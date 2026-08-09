#pragma once

// Opt-in scope timing for on-device profiling, since there is no profiler on Harmattan.
//
//   cmake -DMEEGRAM_PROFILE=ON ...
//
// Wrap a hot function with MEEGRAM_SCOPE("name"). Every 5 seconds a table of
// call counts and accumulated time is written to stderr. Compiles to nothing
// when MEEGRAM_PROFILE is undefined, so it is safe to leave the call sites in.
//
// MEEGRAM_MARK("name") prints one labelled point on a timeline: milliseconds since the
// first marker, milliseconds since the previous one, and the resident set. It is the
// startup instrument - a phase that has no loop to wrap has nothing MEEGRAM_SCOPE can
// measure, so the only way to attribute it is to bracket it with two markers and read
// the delta. main.cpp, ClientProxy.cpp and the QML call sites via Utils::mark lay the
// markers end to end from process entry to the first message list.
//
// The resident-set column is a leftover from what these markers were first written for
// (docs/restructuring.md, whether Qt/QML dominates TDLib's memory) and is kept because
// it costs one /proc read per marker and docs/profiling.md's S0 table is built from it.
// Ignore it when the question is time.
//
// Resident set before and after minimising the window needs no marker at all - both
// samples are reachable from outside the process:
//
//   while :; do grep VmRSS /proc/$(pidof meegram)/status; sleep 5; done

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
//
// Two clocks, because startup needs both. t= is milliseconds since the first marker,
// which is what attributes a phase; the epoch printed once on the header line is what
// attributes everything *before* the first marker. The binary is ~40 MB of mostly
// static TDLib, so the dynamic linker's relocation and demand-paging of it happen
// before main() runs and no in-process clock can see them. Bracket it from outside:
//
//   echo "exec $(date +%s%3N)"; /opt/meegram/bin/meegram 2>&1 | tee log
//
// and subtract that from the header's epoch= to get the pre-main cost.
inline void mark(const char *name)
{
    const std::lock_guard<std::mutex> lock(mutex());

    const auto now = Clock::now();

    static const Clock::time_point first = now;
    static Clock::time_point last = now;

    // A flag rather than `now == first`: two markers can land inside one clock tick,
    // and that would print the header twice.
    static bool headerPrinted = false;

    if (!headerPrinted)
    {
        headerPrinted = true;

        const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        std::fprintf(stderr, "---- MEEGRAM MARK ---- epoch=%lldms\n", static_cast<long long>(epochMs));
    }

    const auto ms = [](Clock::duration d) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
    };

    const long long rss = rssKb();

    // Time first: the elapsed column is what a startup run is read down, and the "+"
    // beside it is the one number that names a phase's cost.
    std::fprintf(stderr, "---- MEEGRAM MARK ---- t=%6lldms  +%6lldms  %-24s %7lld KiB\n", ms(now - first), ms(now - last), name,
                 rss);
    std::fflush(stderr);

    last = now;
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

// Line-numbered so two scopes can share a function - an outer total and an inner
// branch counter is the shape most of these measurements take, and a fixed name makes
// that a redeclaration error.
#define MEEGRAM_SCOPE_CAT_(a, b) a##b
#define MEEGRAM_SCOPE_NAME_(line) MEEGRAM_SCOPE_CAT_(meegramScopeTimer_, line)
#define MEEGRAM_SCOPE(name) const ::profiling::ScopeTimer MEEGRAM_SCOPE_NAME_(__LINE__)(name)
#define MEEGRAM_MARK(name) ::profiling::mark(name)

#else

#define MEEGRAM_SCOPE(name) \
    do                      \
    {                       \
    } while (false)

#define MEEGRAM_MARK(name) \
    do                    \
    {                     \
    } while (false)

#endif
