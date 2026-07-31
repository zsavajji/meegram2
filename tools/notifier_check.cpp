// What meegramd's notifier composes, checked without a phone.
//
// Everything here is a JSON line of the shape TDLib actually emits, fed to the same entry
// points the daemon uses. What it does not cover is the D-Bus half: posting needs
// com.meego.core.MNotificationManager, which exists on the device and nowhere else, so
// this stops at the text and the state. That is the half that has the branches.
//
// Not wired into the build. Build and run it after touching src/daemon/Notifier.cpp:
//
//   cmake --build build-app --target notifier_check && ./build-app/notifier_check

#include "Notifier.hpp"

#include "td/utils/JsonBuilder.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// A decoded line, kept alive with the buffer it points into: td::json_decode unescapes in
// place and every Slice in the result refers back to it.
struct Decoded
{
    explicit Decoded(std::string json)
        : buffer(std::move(json))
    {
        auto result = td::json_decode(td::MutableSlice(buffer));
        assert(result.is_ok());

        value = result.move_as_ok();
        assert(value.type() == td::JsonValue::Type::Object);
    }

    td::JsonObject &object()
    {
        return value.get_object();
    }

    std::string buffer;
    td::JsonValue value;
};

bool contains(const std::string &text, const char *needle)
{
    return text.find(needle) != std::string::npos;
}

}  // namespace

struct NotifierTest
{
    static void run()
    {
        std::vector<std::string> sent;

        Notifier notifier([&sent](const std::string &request) { sent.push_back(request); });

        // Nothing at all happens without this: TDLib defaults notification_group_count_max
        // to 0, which means "this client does not show notifications".
        assert(sent.size() == 1);
        assert(contains(sent[0], "\"notification_group_count_max\""));

        // A chat arrives long before any notification for it, which is why the notifier
        // caches instead of asking.
        update(notifier, R"({"@type":"updateNewChat","chat":{"@type":"chat","id":42,)"
                          R"("type":{"@type":"chatTypePrivate","user_id":7},"title":"Alice",)"
                          R"("photo":{"@type":"chatPhotoInfo","small":{"@type":"file","id":9,)"
                          R"("local":{"@type":"localFile","path":"","can_be_downloaded":true,)"
                          R"("is_downloading_active":false,"is_downloading_completed":false}}}}})");

        assert(notifier.m_chats[42].title == "Alice");
        assert(notifier.m_chats[42].isPrivate);
        assert(notifier.m_chats[42].photoFileId == 9);
        assert(notifier.m_chats[42].photoPath.empty());
        assert(notifier.m_photoOwners[9] == 42);

        // An avatar is only usable once the download has finished - TDLib fills the path
        // in when it starts, and a half-written file draws a broken image.
        update(notifier, R"({"@type":"updateFile","file":{"@type":"file","id":9,)"
                          R"("local":{"@type":"localFile","path":"/home/user/.meegram/a.jpg",)"
                          R"("is_downloading_active":false,"is_downloading_completed":true}}})");

        assert(notifier.m_chats[42].photoPath == "/home/user/.meegram/a.jpg");

        update(notifier, R"({"@type":"updateUser","user":{"@type":"user","id":7,)"
                          R"("first_name":"Alice","last_name":"Smith"}})");

        assert(notifier.m_userNames[7] == "Alice Smith");

        // A private chat says who spoke in the summary, so the body is the message alone.
        Decoded text(newMessage(R"({"@type":"messageText","text":{"@type":"formattedText","text":"Hello"}})"));
        assert(notifier.describe(text.object(), notifier.m_chats[42]) == "Hello");

        // A group repeats it in the body, because the summary is the group's name.
        Notifier::ChatInfo group;
        group.title = "Team";
        group.isPrivate = false;

        Decoded groupText(newMessage(R"({"@type":"messageText","text":{"@type":"formattedText","text":"Hello"}})"));
        assert(notifier.describe(groupText.object(), group) == "Alice Smith: Hello");

        // Attachments read as their type, plus the caption when there is one.
        Decoded photo(newMessage(R"({"@type":"messagePhoto","caption":{"@type":"formattedText","text":"Sunset"}})"));
        assert(notifier.describe(photo.object(), notifier.m_chats[42]) == "Photo: Sunset");

        Decoded sticker(newMessage(R"({"@type":"messageSticker","sticker":{"@type":"sticker","emoji":"X"}})"));
        assert(notifier.describe(sticker.object(), notifier.m_chats[42]) == "X Sticker");

        Decoded document(newMessage(R"({"@type":"messageDocument","document":{"@type":"document","file_name":"cv.pdf"},)"
                                    R"("caption":{"@type":"formattedText","text":""}})"));
        assert(notifier.describe(document.object(), notifier.m_chats[42]) == "cv.pdf");

        // Anything the switch does not know still says something.
        Decoded unknown(newMessage(R"({"@type":"messageChatSetTheme"})"));
        assert(notifier.describe(unknown.object(), notifier.m_chats[42]) == "Unsupported attachment");

        // Previews off is the user's setting, applied by TDLib. Showing the text anyway
        // would be this process overriding it.
        Decoded hidden(R"({"@type":"notification","id":1,"type":{"@type":"notificationTypeNewMessage",)"
                       R"("show_preview":false,"message":{"@type":"message","content":)"
                       R"({"@type":"messageText","text":{"@type":"formattedText","text":"Hello"}}}}})");
        assert(notifier.describe(hidden.object(), notifier.m_chats[42]) == "You have a new message");

        // The strings above are the fallbacks. The language pack replaces them, and the
        // request for it goes out when TDLib says which pack is in use.
        const auto before = sent.size();

        update(notifier, R"({"@type":"updateOption","name":"language_pack_id",)"
                          R"("value":{"@type":"optionValueString","value":"it"}})");

        assert(sent.size() == before + 1);
        assert(contains(sent.back(), "getLanguagePackStrings"));
        assert(contains(sent.back(), "\"it\""));
        assert(contains(sent.back(), "AttachPhoto"));

        // A UI asks for the whole pack at startup, and that response passes through here
        // too. Taking it would put thousands of strings into a process that needs
        // fifteen, so only the one carrying our "@extra" counts.
        update(notifier, R"({"@type":"languagePackStrings","strings":[{"@type":"languagePackString",)"
                          R"("key":"AttachPhoto","value":{"@type":"languagePackStringValueOrdinary",)"
                          R"("value":"Nope"}}],"@extra":"7-12"})");

        assert(notifier.translate("AttachPhoto") == "Photo");

        update(notifier, R"({"@type":"languagePackStrings","strings":[{"@type":"languagePackString",)"
                          R"("key":"AttachPhoto","value":{"@type":"languagePackStringValueOrdinary",)"
                          R"("value":"Foto"}}],"@extra":"meegramd-langpack"})");

        assert(notifier.translate("AttachPhoto") == "Foto");

        Decoded translated(newMessage(R"({"@type":"messagePhoto","caption":{"@type":"formattedText","text":"Sunset"}})"));
        assert(notifier.describe(translated.object(), notifier.m_chats[42]) == "Foto: Sunset");

        // A real supergroup id, not a toy one. Everything here reads ids out of JSON as
        // int64, and on the device that is a 32-bit build where `long` is not one - the
        // exact shape of the bug that cost this codebase weeks on the QML side
        // (main.cpp's double->qlonglong probe). 42 would pass either way; this will not.
        update(notifier, R"({"@type":"updateNewChat","chat":{"@type":"chat",)"
                         R"("id":-1001383801308,"type":{"@type":"chatTypeSupergroup"},"title":"Team"}})");

        assert(notifier.m_chats.find(-1001383801308LL) != notifier.m_chats.end());
        assert(notifier.m_chats[-1001383801308LL].title == "Team");

        notifier.onRequest(R"({"@type":"openChat","chat_id":-1001383801308})");
        assert(notifier.m_openChatId == -1001383801308LL);

        notifier.onRequest(R"({"@type":"closeChat","chat_id":-1001383801308})");

        // The one thing about the UI the update stream does not say: what is on screen.
        notifier.onRequest(R"({"@type":"openChat","chat_id":42})");
        assert(notifier.m_openChatId == 42);

        notifier.onRequest(R"({"@type":"closeChat","chat_id":42})");
        assert(notifier.m_openChatId == 0);

        // A closeChat for a chat that is not the open one must not clear the state - the
        // app closes the page it is leaving after opening the one it is going to.
        notifier.onRequest(R"({"@type":"openChat","chat_id":42})");
        notifier.onRequest(R"({"@type":"closeChat","chat_id":99})");
        assert(notifier.m_openChatId == 42);

        // A notification for the chat the user is reading is not posted. With no bus this
        // could not post anyway, so what is checked is that it returns before asking for
        // the avatar it would otherwise have requested.
        const auto quiet = sent.size();

        update(notifier, R"({"@type":"updateNotificationGroup","notification_group_id":1,"chat_id":42,)"
                          R"("total_count":1,"added_notifications":[{"@type":"notification","id":1,)"
                          R"("type":{"@type":"notificationTypeNewMessage","show_preview":true,)"
                          R"("message":{"@type":"message","content":{"@type":"messagePhoto"}}}}],)"
                          R"("removed_notification_ids":[]})");

        assert(sent.size() == quiet);
        assert(notifier.m_banners.empty());

        // With the chat closed, the same update asks for the avatar it does not have. The
        // banner itself needs a notification manager, which is the device's job.
        notifier.onRequest(R"({"@type":"closeChat","chat_id":42})");
        notifier.m_chats[42].photoPath.clear();

        update(notifier, R"({"@type":"updateNotificationGroup","notification_group_id":1,"chat_id":42,)"
                          R"("total_count":1,"added_notifications":[{"@type":"notification","id":1,)"
                          R"("type":{"@type":"notificationTypeNewMessage","show_preview":true,)"
                          R"("message":{"@type":"message","content":{"@type":"messagePhoto"}}}}],)"
                          R"("removed_notification_ids":[]})");

        assert(sent.size() == quiet + 1);
        assert(contains(sent.back(), "downloadFile"));
        assert(contains(sent.back(), "\"file_id\":9"));

        // Read here or on another device: TDLib empties the group and the banner goes.
        //
        // Deliberately against a stand-in id, even on the device where the update above
        // really did post something. Withdrawing the real one would take it off the screen
        // in the same millisecond it went up, and a banner that never renders is
        // indistinguishable from one that was never posted - which is exactly the question
        // a device run is here to answer. So the real banner stays up to be looked at, and
        // swiped away; only the bookkeeping is checked here.
        notifier.m_banners[1] = Notifier::Banner{42, 5};

        update(notifier, R"({"@type":"updateNotificationGroup","notification_group_id":1,"chat_id":42,)"
                          R"("total_count":0,"added_notifications":[],"removed_notification_ids":[1]})");

        assert(notifier.m_banners.empty());
    }

private:
    // The daemon has the length from td_receive; a literal here does not.
    static void update(Notifier &notifier, const char *line)
    {
        notifier.onUpdate(line, std::strlen(line));
    }

    // A notification carrying one message with the given content.
    static std::string newMessage(const char *content)
    {
        return std::string(R"({"@type":"notification","id":1,"date":0,"is_silent":false,)"
                           R"("type":{"@type":"notificationTypeNewMessage","show_preview":true,)"
                           R"("message":{"@type":"message","id":1,"sender_id":)"
                           R"({"@type":"messageSenderUser","user_id":7},"chat_id":42,"content":)") +
               content + "}}}";
    }
};

int main()
{
    NotifierTest::run();

    std::printf("OK\n");

    return 0;
}
