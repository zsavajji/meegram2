#pragma once

// Opt-in scope timing for on-device profiling, since there is no profiler on Harmattan.
//
//   cmake -DMEEGRAM_PROFILE=ON ...
//
// Wrap a hot function with MEEGRAM_SCOPE("name"). Every 5 seconds a table of
// call counts and accumulated time is written to stderr. Compiles to nothing
// when MEEGRAM_PROFILE is undefined, so it is safe to leave the call sites in.

#ifdef MEEGRAM_PROFILE

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

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

inline void dumpLocked()
{
    std::fprintf(stderr, "---- MEEGRAM PROF ----\n");
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

#else

#define MEEGRAM_SCOPE(name) \
    do                      \
    {                       \
    } while (false)

#endif
