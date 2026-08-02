// What the JSON wire format actually costs on this hardware.
//
// docs/restructuring.md proposes moving TDLib into a service and shipping td_api objects
// over a socket as JSON. The risk was never correctness, it was runtime cost: an encode
// plus a decode per update, continuously, on a 1 GHz Cortex-A8. This measures it against
// a real account rather than a synthetic one.
//
// Method: run the same account twice from an identical database snapshot.
//
//   native  td::ClientManager, td_api C++ objects        - what the app does today
//   json    td_json_client, newline JSON                 - what the service would do
//
// TDLib's own worker threads do the JSON encoding in json mode, and getrusage(RUSAGE_SELF)
// counts every thread, so the CPU-time delta between the two runs IS the encode cost.
// There is no way to time it directly - it happens inside td_receive, which blocks.
//
// The decode side is timed directly, because it is ours to call: json_decode on each
// received line.
//
// What this does NOT measure, and cannot:
//
//   - from_json into a td_api::Object. TDLib generates its JSON codec in Server mode only
//     (td/generate/generate_json.cpp), which yields to_json(Object) and from_json(Function)
//     - the mirror of what a client needs. So the decode figure here is json_decode alone:
//     the parse, without the field mapping that would follow it. A lower bound.
//   - to_json on outbound requests, for the same reason. Less important: the app has ~30
//     call sites against a firehose of inbound updates.
//
// Build: -DMEEGRAM_JSON_BENCH=ON. Needs the TDLib source checkout for JsonBuilder.h, which
// TDLib does not install - see docs/restructuring.md.
//
// Usage:  json_bench <native|json> <database-dir> [seconds]
//
// Run each mode against a *fresh copy* of the database, or the second run has nothing left
// to sync and the comparison is meaningless:
//
//   cp -r ~/.meegram/tdlib /home/user/bench-db
//   json_bench native /home/user/bench-db 90
//   rm -rf /home/user/bench-db && cp -r ~/.meegram/tdlib /home/user/bench-db
//   json_bench json   /home/user/bench-db 90

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_json_client.h>

#include "td/utils/JsonBuilder.h"

#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

// api_id/api_hash/device strings must match src/Common.hpp, or TDLib is answering as a
// different application and the update mix is not the one the app actually sees.
constexpr int ApiId = 142713;
constexpr auto ApiHash = "9e9e687a70150c6436afe3a2b6bfd7d7";
constexpr auto DeviceModel = "Nokia N9";
constexpr auto SystemVersion = "MeeGo 1.2 Harmattan";
constexpr auto AppVersion = "0.3.1";

double cpuSeconds()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);

    const auto toSeconds = [](const timeval &t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6; };

    return toSeconds(usage.ru_utime) + toSeconds(usage.ru_stime);
}

struct Totals
{
    long long objects = 0;
    long long bytes = 0;
    long long decodeNs = 0;
};

void report(const char *mode, const Totals &totals, double wall, double cpu)
{
    std::printf("\n==== json_bench: %s ====\n", mode);
    std::printf("  wall              %8.2f s\n", wall);
    std::printf("  cpu               %8.2f s   (%.1f%% of wall)\n", cpu, 100.0 * cpu / wall);
    std::printf("  objects received  %8lld\n", totals.objects);

    if (totals.objects > 0)
        std::printf("  cpu per object    %8.1f us\n", cpu * 1e6 / static_cast<double>(totals.objects));

    if (totals.bytes > 0)
    {
        std::printf("  bytes             %8lld  (%.1f KiB)\n", totals.bytes, static_cast<double>(totals.bytes) / 1024.0);
        std::printf("  mean object size  %8.0f bytes\n", static_cast<double>(totals.bytes) / static_cast<double>(totals.objects));

        const double decodeMs = static_cast<double>(totals.decodeNs) / 1e6;
        std::printf("  json_decode total %8.1f ms  (%.1f%% of cpu)\n", decodeMs, 100.0 * (decodeMs / 1e3) / cpu);
        std::printf("  json_decode each  %8.1f us  (parse only, no field mapping)\n",
                    static_cast<double>(totals.decodeNs) / 1e3 / static_cast<double>(totals.objects));
    }

    std::fflush(stdout);
}

std::string tdlibParametersJson(const char *databaseDirectory)
{
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"@type":"setTdlibParameters","database_directory":"%s","use_file_database":true,)"
                  R"("use_chat_info_database":true,"use_message_database":true,"use_secret_chats":true,)"
                  R"("api_id":%d,"api_hash":"%s","system_language_code":"en","device_model":"%s",)"
                  R"("system_version":"%s","application_version":"%s"})",
                  databaseDirectory, ApiId, ApiHash, DeviceModel, SystemVersion, AppVersion);

    return buffer;
}

Totals runJson(const char *databaseDirectory, double seconds)
{
    Totals totals;

    const int clientId = td_create_client_id();

    // td_create_client_id does not create anything until the first request is sent.
    const auto parameters = tdlibParametersJson(databaseDirectory);
    td_send(clientId, parameters.c_str());

    const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);

    while (Clock::now() < deadline)
    {
        const char *line = td_receive(1.0);
        if (!line)
            continue;

        const auto length = std::strlen(line);

        totals.objects++;
        totals.bytes += static_cast<long long>(length);

        // json_decode takes a MutableSlice and unescapes in place, so the buffer TDLib
        // owns has to be copied first. The service would be copying it anyway - td_receive
        // invalidates the pointer on the next call - so this copy is part of the real cost.
        std::string mutableLine(line, length);

        const auto start = Clock::now();
        auto decoded = td::json_decode(td::MutableSlice(mutableLine));
        totals.decodeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();

        if (decoded.is_error())
            std::fprintf(stderr, "json_decode failed: %s\n", decoded.error().message().str().c_str());
    }

    return totals;
}

Totals runNative(const char *databaseDirectory, double seconds)
{
    Totals totals;

    // Deliberately leaked, and the leak is the point. As a local, its destructor ran
    // inside the timed region and blocked flushing sqlite to disk - while the json arm
    // uses td_json_client, which is never destroyed and simply abandons that work at
    // exit. The first run of this benchmark charged native ~48 s of wall and an unknown
    // slice of CPU for a clean shutdown json never performed, and duly reported JSON as
    // the cheaper of the two.
    //
    // Both arms now abandon at the same point, which is also the honest comparison for a
    // daemon: it runs continuously, so what shutdown costs is not what we are pricing.
    auto *manager = new td::ClientManager();
    const auto clientId = manager->create_client_id();

    auto parameters = td::td_api::make_object<td::td_api::setTdlibParameters>();
    parameters->database_directory_ = databaseDirectory;
    parameters->use_file_database_ = true;
    parameters->use_chat_info_database_ = true;
    parameters->use_message_database_ = true;
    parameters->use_secret_chats_ = true;
    parameters->api_id_ = ApiId;
    parameters->api_hash_ = ApiHash;
    parameters->system_language_code_ = "en";
    parameters->device_model_ = DeviceModel;
    parameters->system_version_ = SystemVersion;
    parameters->application_version_ = AppVersion;

    manager->send(clientId, 1, std::move(parameters));

    const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);

    while (Clock::now() < deadline)
    {
        auto response = manager->receive(1.0);
        if (response.object)
            totals.objects++;
    }

    return totals;
}

}  // namespace

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <native|json> <database-dir> [seconds]\n", argv[0]);
        return 2;
    }

    const char *mode = argv[1];
    const char *databaseDirectory = argv[2];
    const double seconds = argc > 3 ? std::atof(argv[3]) : 90.0;

    const bool isJson = std::strcmp(mode, "json") == 0;
    if (!isJson && std::strcmp(mode, "native") != 0)
    {
        std::fprintf(stderr, "mode must be 'native' or 'json'\n");
        return 2;
    }

    // Matches the app (src/Client.cpp), so TDLib's own logging is not part of the cost.
    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1));

    const auto wallStart = Clock::now();
    const double cpuStart = cpuSeconds();

    const Totals totals = isJson ? runJson(databaseDirectory, seconds) : runNative(databaseDirectory, seconds);

    const double wall = std::chrono::duration<double>(Clock::now() - wallStart).count();
    const double cpu = cpuSeconds() - cpuStart;

    report(mode, totals, wall, cpu);

    return 0;
}
