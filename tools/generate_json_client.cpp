//
// Emits the client-direction half of TDLib's JSON codec.
//
// TDLib ships only the server direction. td/generate/generate_json.cpp invokes the
// generator once, with TL_writer::Server, which yields:
//
//     to_json(Object)      encode updates and responses
//     from_json(Function)  decode incoming requests
//
// That is correct for TDLib, whose only JSON consumer is td_json_client: it receives
// functions and emits objects, and never needs the other direction. It is exactly the
// wrong half for a UI talking to a relay, which needs the mirror:
//
//     to_json(Function)    encode outbound requests
//     from_json(Object)    decode inbound updates
//
// The generator already supports Mode::Client and emits precisely that pair. This file
// is td/generate/generate_json.cpp with the mode flipped and the output renamed -
// gen_json_converter() is public API in td/generate/tl_json_converter.h.
//
// Mode::Client does need one patch inside td/, though. tl_json_converter.cpp had no
// to_json for vector<bytes>; it emitted the literal placeholder "UNSUPPORTED STORED
// VECTOR OF BYTES". Upstream never trips it because server mode only emits to_json for
// Objects, and no Object has such a field. Client mode emits to_json for queries, two of
// which do (inputPassportElementErrorSourceFiles and ...TranslationFiles, file_hashes).
// So td/tl/tl_json.h gains a JsonVectorBytes - base64 per element, mirroring the scalar
// bytes path - and tl_json_converter.cpp emits it instead of the placeholder.
//
// That is one of two local patches in the submodule; the other is unrelated to JSON
// (TD_HAS_MMSG forced to 0 in tdutils/td/utils/port/config.h). tools/setup-dependencies.sh
// applies both idempotently on every run, so a td/ bump reapplies them by itself - see
// patch_td_json_vector_bytes, which also fails loudly if upstream moves what it anchors on.
//
// One wrinkle, and it is why src/JsonCodec.cpp exists. gen_json_converter_file() emits a
// fixed trailer - three functions and their declarations - that is hardcoded to the
// server direction regardless of mode (tl_json_converter.cpp:265-296). Under Mode::Client
// it does not compile: its to_json(const Object &) switch downcasts Function subclasses to
// Object, and its from_json(object_ptr<Function> &) needs per-function decoders that
// client mode does not generate.
//
// That trailer lands in file 0 and *only* in file 0. is_suitable(0, 10, counter) tests
// `counter % 9 == -1`, which is never true, so file 0 carries no per-class functions at
// all - it is trailer and nothing else. So the build simply does not compile
// td_api_json_client_0.cpp, and src/JsonCodec.cpp hand-writes the three replacements.
// Files 1-9 carry every generated per-class function and are used as-is.
//
// Built and run by tools/setup-dependencies.sh, in td/generate/auto, alongside TDLib's
// own generators. Re-run whenever the td/ submodule moves.

#include "tl_json_converter.h"

#include "td/tl/tl_config.h"
#include "td/tl/tl_generate.h"

int main()
{
    // Paths are relative to the working directory, which must be td/generate/auto -
    // the same directory TDLib runs its own generators in.
    //
    // 10 source files, matching TDLib's own split. The count is not cosmetic: these
    // translation units are enormous, and one of them is what tools/setup-dependencies.sh
    // caps its job count for.
    td::gen_json_converter(td::tl::read_tl_config_from_file("tlo/td_api.tlo"), "td/telegram/td_api_json_client",
                           td::tl::TL_writer::Client, 10);
}
