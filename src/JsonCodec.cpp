#include "JsonCodec.hpp"

#include "td/telegram/td_api.hpp"

#include "td/tl/tl_json.h"

namespace td
{
namespace td_api
{

void to_json(JsonValueScope &jv, const Function &object)
{
    // Same shape the generator uses for every polymorphic type with more than one
    // constructor (tl_json_converter.cpp:103-107). downcast_call takes a mutable
    // reference purely to switch on get_id(); the generic lambda binds by const
    // reference, so nothing is actually mutated.
    downcast_call(const_cast<Function &>(object), [&jv](const auto &request) { to_json(jv, request); });
}

void to_json(JsonValueScope &jv, const object_ptr<Function> &value)
{
    // td::to_json, not this overload again: the template in tl_json.h handles the null
    // case and then calls back into the Function dispatcher above.
    td::to_json(jv, value);
}

Status from_json(object_ptr<Object> &to, td::JsonValue from)
{
    // The template this resolves to needs three things, all of which the client-mode
    // generator emits: tl_constructor_from_string(Object *) to map "@type" to an id,
    // downcast_call to construct that id's class, and a per-class from_json to fill it.
    return td::from_json(to, std::move(from));
}

}  // namespace td_api
}  // namespace td
