#pragma once

// Where both processes' stderr goes, and how it is kept from growing without bound.
//
// Neither of them can rely on inheriting a useful stderr. meegramd normally starts by
// D-Bus activation, which hands it dbus-daemon's; the app normally starts through
// invoker, which discards it outright (docs/troubleshooting.md, "Nothing appears in the
// log at all"). Between them that is every diagnostic either one writes - a rejected
// peer, a socket never found, the startup stall - going to a descriptor pointed at
// nothing on exactly the launches nobody can attach a terminal to.
//
// So point the descriptor at a file. This is not a logging framework and it changes no
// call site: the twenty-odd fprintf(stderr) calls in the daemon, every qWarning in the
// app - Qt's default handler is an fprintf to stderr - and anything TDLib or the GL
// driver prints all land in it because they already wrote there.
//
// No Qt, deliberately: meegramd links none, and the whole argument for its 24 MiB
// resident set is that it never will.

#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#ifdef MEEGRAM_FILE_LOG

// Set by openLog, empty until then and if it failed - which is what stops rotateLogIfLarge
// from renaming a file this process does not own.
//
// A function-local static rather than a global, so this header stays one file with no
// translation unit of its own.
inline std::string &logFilePath()
{
    static std::string path;
    return path;
}

// One rotation kept, so the ceiling is 512 KiB per process on a device whose eMMC the
// user cares about. The interesting lines are always the most recent ones - the tap that
// just failed, the peer just rejected - and 256 KiB is over a week of an offline daemon
// reporting a retry every 30 seconds, which is its chattiest idle state.
constexpr off_t MaxLogBytes = 256 * 1024;

// $HOME, not $XDG_RUNTIME_DIR where the daemon's socket goes: that is tmpfs and is wiped
// at logout, and a log that cannot survive the reboot you are trying to explain is not
// worth writing.
//
// dup2 rather than freopen, here and in the rotation below. freopen closes the old stream
// *before* it tries to open the new one, so a failure leaves the process with no stderr at
// all; and it works on the FILE*, which another thread can be inside at the time. Swapping
// the descriptor underneath the FILE* is atomic in the kernel, so a line racing a rotation
// lands in one file or the other and both are on disk.
inline void openLog(const char *fileName)
{
    const char *home = std::getenv("HOME");

    const std::string path = std::string(home ? home : ".") + "/.meegram/" + fileName;

    // The directory is TDLib's, and TDLib does not create it until a UI has sent
    // setTdlibParameters - so on a fresh device it does not exist yet, and neither this nor
    // the daemon's $HOME socket fallback can be created in it. Owner-only: both are
    // session data.
    ::mkdir(path.substr(0, path.rfind('/')).c_str(), S_IRWXU);

    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        std::fprintf(stderr, "log: cannot open %s (%s); leaving stderr as it came\n", path.c_str(), std::strerror(errno));
        return;
    }

    // On the inherited stderr, before it is replaced: run by hand, this is the one line
    // that says where everything after it went.
    std::fprintf(stderr, "log: writing to %s\n", path.c_str());

    ::dup2(fd, STDERR_FILENO);
    ::close(fd);

    logFilePath() = path;
}

inline void rotateLogIfLarge()
{
    // Both callers in meegramd are on different threads - the poll loop while a UI is
    // attached, the receive loop when nothing else is happening - and two of them renaming
    // at once would leave the second clobbering the .1 the first had just written. The app
    // logs from its TDLib reader thread as well as the main one.
    static std::mutex mutex;

    const std::lock_guard<std::mutex> lock(mutex);

    if (logFilePath().empty())
        return;

    struct stat status;

    // Not a regular file means stderr has been pointed somewhere else since - and a size
    // on a terminal or a pipe means nothing anyway.
    if (::fstat(STDERR_FILENO, &status) < 0 || !S_ISREG(status.st_mode) || status.st_size < MaxLogBytes)
        return;

    const std::string &path = logFilePath();

    if (::rename(path.c_str(), (path + ".1").c_str()) < 0)
        return;

    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        // stderr still points at the file that was just renamed, so nothing is lost and
        // nothing is broken - it keeps writing to what is now the .1. The next check finds
        // it still oversized and tries again.
        return;
    }

    ::dup2(fd, STDERR_FILENO);
    ::close(fd);
}

#else

// -DMEEGRAM_FILE_LOG=OFF: stderr stays exactly as it was handed over. Stubs rather than
// #ifdefs at the call sites - one branch here beats five scattered through two processes,
// and all of them compile away.
inline void openLog(const char *)
{
}

inline void rotateLogIfLarge()
{
}

#endif
