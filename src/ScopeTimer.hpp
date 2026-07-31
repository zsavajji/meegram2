#pragma once

// Opt-in scope timing for on-device profiling, since there is no profiler on Harmattan.
//
//   cmake -DMEEGRAM_PROFILE=ON ...
//
// Wrap a hot function with MEEGRAM_SCOPE("name"). Every 5 seconds a table of
// call counts and accumulated time is written to stderr. Compiles to nothing
// when MEEGRAM_PROFILE is undefined, so it is safe to leave the call sites in.
//
// MEEGRAM_RSS("name") prints one labelled resident-set reading. It exists to settle
// the question docs/restructuring.md opens: whether the Qt/QML side really dominates
// TDLib's memory, which is what decides whether moving TDLib into a service reclaims
// anything. The startup markers in main.cpp straddle the boundaries that separate the
// two, so the deltas between consecutive lines attribute the RSS.
//
// The other half of that measurement - resident set before and after minimising the
// window - needs no code and no marker, because both samples are reachable from
// outside the process:
//
//   while :; do grep VmRSS /proc/$(pidof meegram)/status; sleep 5; done
//
// Read a few lines, minimise, read a few more. Sampling that from inside would mean
// an idle timer, and an idle timer is exactly the thing whose cost would perturb the
// number it is trying to report.

#ifdef MEEGRAM_PROFILE

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include <unistd.h>

namespace profiling {

using Clock = std::chrono::steady_clock;

struct Site
{
    unsigned long long calls = 0;
    long long totalNs = 0;
};

// Keyed by the literal's contents, not its address, so the same name used from two
// translation units collapses into one row. Comparing char pointers avoids
// constructing a std::string on every record().
struct StrLess
{
    bool operator()(const char *a, const char *b) const noexcept { return std::strcmp(a, b) < 0; }
};

inline std::mutex &mutex()
{
    static std::mutex m;
    return m;
}

inline std::map<const char *, Site, StrLess> &sites()
{
    static std::map<const char *, Site, StrLess> s;
    return s;
}

// Resident set in KiB, or 0 if it could not be read. /proc/self/statm rather than
// /proc/self/status: it is two lines of integers instead of fifty of formatted text,
// and the second field is the resident page count. Cheap enough to call from a marker
// without the reading disturbing what it measures.
inline long long rssKb()
{
    std::FILE *f = std::fopen("/proc/self/statm", "r");
    if (!f)
        return 0;

    unsigned long long residentPages = 0;
    // %*s for the skipped first field, not %*llu: a length modifier on a suppressed
    // conversion is a -Wformat warning, and -Werror is on in Debug builds.
    const int matched = std::fscanf(f, "%*s %llu", &residentPages);
    std::fclose(f);

    if (matched != 1)
        return 0;

    return static_cast<long long>(residentPages) * (sysconf(_SC_PAGESIZE) / 1024);
}

// Printed immediately rather than accumulated into the table below: these are a
// timeline, so their order carries the meaning, and a delta against the previous
// marker is the number actually being read off.
inline void mark(const char *name)
{
    const std::lock_guard<std::mutex> lock(mutex());

    static long long previous = 0;

    const long long rss = rssKb();

    std::fprintf(stderr, "---- MEEGRAM RSS ---- %-22s %7lld KiB  (%+lld)\n", name, rss, rss - previous);
    std::fflush(stderr);

    previous = rss;
}

inline void dumpLocked()
{
    std::fprintf(stderr, "---- MEEGRAM PROF ---- rss=%lld KiB\n", rssKb());
    for (const auto &[name, site] : sites())
    {
        const double totalMs = static_cast<double>(site.totalNs) / 1e6;
        const double avgUs = site.calls ? (static_cast<double>(site.totalNs) / 1e3) / static_cast<double>(site.calls) : 0.0;
        std::fprintf(stderr, "  %-34s calls=%-9llu total=%9.1fms avg=%9.2fus\n", name, site.calls, totalMs, avgUs);
    }
    std::fflush(stderr);
}

// The instrumented sites are all on the GUI thread, so this mutex is uncontended;
// it is here only so an accidental call from the TDLib worker cannot corrupt the map.
inline void record(const char *name, long long ns)
{
    const std::lock_guard<std::mutex> lock(mutex());

    auto &site = sites()[name];
    site.calls++;
    site.totalNs += ns;

    static Clock::time_point lastDump = Clock::now();

    if (const auto now = Clock::now(); now - lastDump >= std::chrono::seconds(5))
    {
        lastDump = now;
        dumpLocked();
    }
}

class ScopeTimer
{
public:
    explicit ScopeTimer(const char *name) noexcept
        : m_name(name)
        , m_start(Clock::now())
    {
    }

    ~ScopeTimer() { record(m_name, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count()); }

    ScopeTimer(const ScopeTimer &) = delete;
    ScopeTimer &operator=(const ScopeTimer &) = delete;

private:
    const char *m_name;
    Clock::time_point m_start;
};

}  // namespace profiling

#define MEEGRAM_SCOPE(name) const ::profiling::ScopeTimer meegramScopeTimer_(name)
#define MEEGRAM_RSS(name) ::profiling::mark(name)

#else

#define MEEGRAM_SCOPE(name) \
    do                      \
    {                       \
    } while (false)

#define MEEGRAM_RSS(name) \
    do                    \
    {                     \
    } while (false)

#endif
