// Proves the client-direction JSON codec carries both directions intact, and in
// particular that 64-bit ids survive the wire.
//
// Ids are the failure this codebase has already been bitten by: a message id is
// `id << 20`, and a decoder that materialises JSON numbers as doubles hands it back with
// the low bits wrong (docs/architecture.md, and the QML-side string invariant that exists
// for the same reason). TDLib's own parser keeps numbers as text and converts with
// to_integer_safe, so it is exact - but that is a property worth pinning down with an
// assertion rather than trusting, because the whole transport rests on it.
//
// The values below are deliberately near the top of the int53 range TDLib guarantees for
// ids. A small id passes whatever the decoder does and proves nothing.
//
// Build: -DMEEGRAM_JSON_TRANSPORT=ON, target json_roundtrip. No framework, no daemon, no
// network - the codec alone.

#include "JsonCodec.hpp"

#include "td/utils/JsonBuilder.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// Supergroup-shaped, ~1.0e15, well past the 2^32 and 2^24 boundaries a truncating decoder
// would trip on.
constexpr std::int64_t ChatId = -1001234567890123LL;

// 1000000000 << 20, the real shape of a message id, ~1.05e15.
constexpr std::int64_t MessageId = 1048576000000000LL;

void checkOutbound()
{
    // Held as object_ptr<Function>, not as the concrete type, because that is what
    // Client::send passes - and it is what routes the call through the hand-written
    // to_json(const Function &) dispatcher in JsonCodec.cpp. Encoding the concrete type
    // would bypass exactly the piece this file exists to check.
    td::td_api::object_ptr<td::td_api::Function> request =
        td::td_api::make_object<td::td_api::getChatHistory>(ChatId, MessageId, 0, 50, false);

    const auto json = td::json_encode<std::string>(td::ToJson(request));

    std::printf("outbound: %s\n", json.c_str());

    assert(json.find(R"("@type":"getChatHistory")") != std::string::npos);

    // Bare numbers, not quoted. Ids are int53 in the schema, and the generator only wraps
    // a field in JsonInt64 - which would quote it - when the schema says int64. Asserting
    // the exact digits is the point: it is what fails if anything on this path goes
    // through a double.
    assert(json.find(R"("chat_id":-1001234567890123)") != std::string::npos);
    assert(json.find(R"("from_message_id":1048576000000000)") != std::string::npos);
}

void decodeUpdateAndCheckIds(const char *what, std::string line)
{
    auto r_value = td::json_decode(td::MutableSlice(line));
    assert(r_value.is_ok());

    td::td_api::object_ptr<td::td_api::Object> object;
    auto status = td::td_api::from_json(object, r_value.move_as_ok());

    if (status.is_error())
        std::printf("%s decode failed: %s\n", what, status.message().str().c_str());

    assert(status.is_ok());
    assert(object != nullptr);
    assert(object->get_id() == td::td_api::updateNewMessage::ID);

    const auto &update = static_cast<const td::td_api::updateNewMessage &>(*object);
    assert(update.message_ != nullptr);

    std::printf("%s: chat_id=%lld id=%lld\n", what, static_cast<long long>(update.message_->chat_id_),
                static_cast<long long>(update.message_->id_));

    assert(update.message_->chat_id_ == ChatId);
    assert(update.message_->id_ == MessageId);
    assert(update.message_->is_outgoing_ == true);

    assert(update.message_->content_ != nullptr);
    assert(update.message_->content_->get_id() == td::td_api::messageText::ID);

    const auto &content = static_cast<const td::td_api::messageText &>(*update.message_->content_);
    assert(content.text_ != nullptr);
    assert(content.text_->text_ == "ciao");
}

void checkInbound()
{
    // Hand-written rather than produced by to_json: the point is to decode what meegramd
    // actually forwards, which is TDLib's output, not ours. Ids bare, as TDLib emits them
    // for int53.
    decodeUpdateAndCheckIds("inbound",
                            R"({"@type":"updateNewMessage","message":{"@type":"message","id":1048576000000000,)"
                            R"("chat_id":-1001234567890123,"is_outgoing":true,"date":1753920000,)"
                            R"("content":{"@type":"messageText","text":{"@type":"formattedText","text":"ciao",)"
                            R"("entities":[]}}}})");
}

void checkInboundQuotedIds()
{
    // The same update with ids as strings. TDLib emits this form for true int64 fields
    // (JsonInt64), and from_json accepts either - so both shapes have to land on the same
    // value. If this one drifts, a field that changes from int53 to int64 in a future
    // schema breaks silently.
    decodeUpdateAndCheckIds("inbound-quoted",
                            R"({"@type":"updateNewMessage","message":{"@type":"message","id":"1048576000000000",)"
                            R"("chat_id":"-1001234567890123","is_outgoing":true,"date":1753920000,)"
                            R"("content":{"@type":"messageText","text":{"@type":"formattedText","text":"ciao",)"
                            R"("entities":[]}}}})");
}

// A query-only type is not in the client-direction constructor table at all, so it fails
// at the "@type" lookup rather than reaching a decoder. Asserting that it is an error and
// not a silently empty object is what would catch a codec generated in the wrong mode.
void checkUndecodableIsRejected()
{
    std::string line = R"({"@type":"inputMessageText","text":{"@type":"formattedText","text":"x"}})";

    auto r_value = td::json_decode(td::MutableSlice(line));
    assert(r_value.is_ok());

    td::td_api::object_ptr<td::td_api::Object> object;
    auto status = td::td_api::from_json(object, r_value.move_as_ok());

    assert(status.is_error());

    std::printf("rejected as expected: %s\n", status.message().str().c_str());
}

}  // namespace

int main()
{
    checkOutbound();
    checkInbound();
    checkInboundQuotedIds();
    checkUndecodableIsRejected();

    std::printf("json_roundtrip: OK\n");

    return 0;
}
