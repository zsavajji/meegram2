#pragma once

// The client half of TDLib's JSON codec.
//
// tools/generate_json_client.cpp emits the per-class encoders and decoders that TDLib
// itself does not ship - to_json for every Function, from_json for every result type.
// What it cannot emit is the three entry points that tie them together: the generator's
// trailer is hardcoded to the server direction and lands, alone, in file 0 of the
// generated set. The build skips that file (see CMakeLists.txt) and this header declares
// the three replacements, defined in JsonCodec.cpp.
//
// They live in namespace td::td_api because that is where the generated per-class
// functions live, and because unqualified lookup inside TDLib's own tl_json.h templates
// is what has to find them.
//
// Their signatures are the exact mirror of the ones td_api_json_client.h declares in its
// trailer. Those server-direction declarations are still visible - harmless, because
// nothing calls them and they are never defined - so both trios coexist as overloads.

// Qt4 defines `foreach` as a macro unless QT_NO_KEYWORDS, and td::JsonObject has a member
// function of that name - so any translation unit that reaches Qt before TDLib's
// JsonBuilder.h fails to compile it. Undefined here rather than fixed by include order,
// because include order is not something clang-format can be trusted to leave alone.
// Nothing in src/ uses the keyword, so it is not restored afterwards.
#ifdef foreach
#undef foreach
#endif

#include "td/telegram/td_api_json_client.h"

// The generic half of the codec: ToJson, the object_ptr and vector overloads, and the
// scalar from_json cases the generated per-class functions call into. Included here
// rather than left to callers because the calls inside TDLib's own templates are
// dependent and resolve by argument-dependent lookup at the point of instantiation - so
// a translation unit that encodes a request without this header in scope fails with a
// missing operator<< rather than a missing to_json.
#include "td/tl/tl_json.h"

namespace td
{
namespace td_api
{

// Decoder of last resort, and the reason the generated set stays at client size.
//
// from_json(object_ptr<Object> &) dispatches through downcast_call(Object &), which
// enumerates *every* Object subclass. Client mode only generates decoders for result
// types, so the query-only ones - a sort order handed to a function, say - reach that
// switch with no overload to call and the instantiation fails to compile.
//
// Those types are unreachable inbound by construction: they are function arguments, so
// they travel UI -> daemon and never arrive. The alternative is generating both
// directions for everything (TL_writer::All), which compiles 840 KB more C++ into the
// binary, all of it decoders that cannot run. Measured, not guessed.
//
// The generated per-class overloads are non-templates and win resolution outright, so
// this only ever binds the types that have none. If it does fire, the wire carried
// something the UI is not supposed to receive, and an error is the honest answer.
template <class T>
Status from_json(T &, JsonObject &)
{
    return Status::Error(400, "Type is not decodable in the client direction");
}

// Dispatches an outbound request to its generated per-function encoder.
void to_json(JsonValueScope &jv, const Function &object);

// The object_ptr overload TDLib's ToJson() lands on when given a request.
void to_json(JsonValueScope &jv, const object_ptr<Function> &value);

// Decodes an inbound update or response. Reads "@type" and builds the concrete class.
//
// td::JsonValue must stay qualified: td_api has a JsonValue class of its own (the
// tdweb API type), and unqualified it wins lookup here and is abstract.
Status from_json(object_ptr<Object> &to, td::JsonValue from);

}  // namespace td_api
}  // namespace td
