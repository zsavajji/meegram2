// meegramd - the resident half of MeeGram.
//
// Owns the TDLib connection and its database lock, so closing the UI window stops
// closing the Telegram connection (docs/restructuring.md). Two jobs, and the split
// between them is the whole design:
//
//   - the relay, which is this file. It speaks td_json_client - the installed public C
//     API - and never links td_api or Qt. Bytes in from a socket go to td_send; bytes out
//     of td_receive go to every connected socket, unexamined.
//
//   - the notifier, src/daemon/Notifier.cpp, which posts the system notifications. That
//     is the part the UI used to own, and owning it here is what makes a closed app still
//     notify.
//
// The relay does not parse the JSON it carries. Nothing here needs to: requests are
// opaque, and responses are matched by the UI on "@extra", which the UI makes globally
// unique so two UIs cannot collide (see Client::send in src/ClientProxy.cpp). That is why
// there is no per-connection @extra rewriting and no request routing table - a second UI
// receives a foreign response, fails to find a handler for its @extra, and drops it,
// which is what today's Client already does with an unmatched request id.
//
// The notifier does parse, on a copy, after the line has been relayed - so a UI never
// waits on it. It reads tdutils' JSON, not the generated td_api codec: a preview needs a
// dozen fields, and binding 1.2 MB of generated C++ into this process to read them would
// undo the resident-set argument the daemon exists for.
//
// Framing is one JSON object per line. TDLib emits compact single-line JSON and escapes
// every control character inside strings, so a raw newline never appears in a payload.

#include "Notifier.hpp"

#include <td/telegram/td_json_client.h>

#include <dbus/dbus.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

// $XDG_RUNTIME_DIR is the right home for a session socket - tmpfs, user-private, and
// cleaned up at logout. Harmattan predates it, hence the fallback, which is also where
// AppManager already puts the TDLib database.
std::string socketPath()
{
    if (const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir)
        return std::string(runtimeDir) + "/meegram.sock";

    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.meegram/sock";
}

// The read buffer travels with its socket rather than in a parallel array. A read()
// boundary has nothing to do with a line boundary, so a request can arrive in pieces and
// two can arrive together - and when a connection drops, its half-line has to drop with
// it, at the same index, which a second vector gets wrong the first time anyone
// disconnects.
struct Connection
{
    int fd;

    // Inbound, waiting for its terminating newline.
    std::string pending;

    // Outbound, waiting for the socket to accept it. This queue is what stops the daemon
    // from ever blocking inside a write, and it is not an optimisation - without it the
    // two processes deadlock:
    //
    //   the UI's reader thread blocks writing a request (send() from inside a response
    //   callback, which is where callbacks run) -> it stops reading -> this daemon's
    //   broadcast blocks writing to it, holding connectionsMutex -> the poll loop cannot
    //   take that lock to drain the request that would have unblocked everything.
    //
    // Both socket buffers have to fill for that to close, which needs a burst - initial
    // sync produces exactly the burst. So writes here are non-blocking and the remainder
    // lands in this queue for the poll loop to finish on POLLOUT.
    std::string outgoing;
};

// Backpressure has to end somewhere: a client that never reads would otherwise grow this
// queue until the daemon is OOM-killed, and on Harmattan that means the low-memory killer
// takes the resident process this whole design exists to keep alive. 8 MiB is ~45 seconds
// of the 188 KiB/s peak measured during initial sync (docs/restructuring.md) - far past
// "briefly busy" and well short of dangerous.
constexpr size_t MaxOutgoingBytes = 8u << 20;

// Touched by both threads: the receive loop broadcasts, the poll loop adds and removes.
std::mutex connectionsMutex;
std::vector<Connection> connections;

// Never deleted, deliberately. The receive thread is detached and parked in td_receive
// with no way to be woken, so a notifier destroyed when main returns is one the other
// thread can still be inside. A process-lifetime leak of one object is the cheaper half
// of that trade.
Notifier *notifier = nullptr;

// How often to prod TDLib into retrying while it is not connected. Matches the td_receive
// timeout, so on a quiet offline client the prod lands on the very tick that wakes the
// receive loop and nothing else has to keep time.
constexpr double NudgeIntervalSeconds = 30.0;

// Reconnection, which TDLib will not do on its own here.
//
// It retries by itself, but every retry goes through a per-client backoff and three flood
// controls, and the only thing that clears them is ConnectionCreator::on_network - reached
// from setNetworkType, and from nothing else. A client that never calls it has no way to
// say "the network is back, stop waiting", so a daemon started with no network stayed in
// its retry pattern long after wifi appeared. Killing and restarting it was the only way
// out, which is a fine description of the bug and a poor description of a daemon.
//
// A phone is exactly the device this happens on: it boots, something starts, and the
// network arrives a minute later.
//
// So: while TDLib says it is not connected, send setNetworkType every 30 seconds.
// StateManager bumps its generation whether or not the type changed, so the same value
// resent is still the signal - which means this needs no idea of what the real network is
// doing, and cannot be wrong about it. It costs one local request per 30s while offline
// and stops entirely once connected.
//
// Deliberately not ICd2 (com.nokia.icd2 state_sig, the platform's own connectivity
// signal): it would be more precise and it would be a second D-Bus dialect to get right,
// for a wakeup this already gets for free.
void nudgeIfOffline(int clientId, std::optional<bool> connected)
{
    static auto lastNudge = std::chrono::steady_clock::time_point{};

    // Empty until TDLib has reported a connection state at all, which it does not do
    // before a client has sent setTdlibParameters. A daemon nobody has connected to yet is
    // not offline, it is idle - and saying "not connected" about it sends whoever is
    // reading the log after the wrong problem, which is exactly what it did.
    if (!connected.has_value() || *connected)
        return;

    const auto now = std::chrono::steady_clock::now();

    if (lastNudge != std::chrono::steady_clock::time_point{} &&
        std::chrono::duration<double>(now - lastNudge).count() < NudgeIntervalSeconds)
        return;

    lastNudge = now;

    std::fprintf(stderr, "meegramd: not connected; asking TDLib to retry\n");

    td_send(clientId, R"({"@type":"setNetworkType","type":{"@type":"networkTypeOther"},"@extra":"meegramd-network"})");
}

// Whether TDLib has just said it is connected, or empty if this line is not about the
// connection at all. The update carries exactly one state object, so this needs no parse -
// and the relay stays a relay.
//
// Updating counts as connected: it means the connection is up and TDLib is draining the
// backlog behind it, which is the one state where prodding it would be actively unhelpful.
std::optional<bool> connectionState(const char *line, size_t length)
{
    constexpr char Prefix[] = "{\"@type\":\"updateConnectionState\"";

    if (length < sizeof(Prefix) - 1 || std::memcmp(line, Prefix, sizeof(Prefix) - 1) != 0)
        return std::nullopt;

    return std::strstr(line, "connectionStateReady") != nullptr || std::strstr(line, "connectionStateUpdating") != nullptr;
}

// Returns the following element, so it can drive a loop that erases as it walks.
// Callers must already hold connectionsMutex.
std::vector<Connection>::iterator closeConnection(std::vector<Connection>::iterator it)
{
    ::close(it->fd);
    return connections.erase(it);
}

// Writes as much of the queue as the socket will take without blocking. Returns false if
// the connection is dead and should be dropped. Callers must hold connectionsMutex.
bool flushOutgoing(Connection &connection)
{
    while (!connection.outgoing.empty())
    {
        // MSG_NOSIGNAL: a UI that exits between the poll and this write would otherwise
        // deliver SIGPIPE and kill the daemon - which is precisely the process that is
        // supposed to outlive it.
        const ssize_t n = ::send(connection.fd, connection.outgoing.data(), connection.outgoing.size(), MSG_NOSIGNAL);
        if (n > 0)
        {
            connection.outgoing.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;

        // Socket full. The poll loop finishes this on POLLOUT; the connection is fine.
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true;

        return false;
    }

    return true;
}

void broadcast(const char *line, size_t length)
{
    const std::lock_guard<std::mutex> lock(connectionsMutex);

    for (auto it = connections.begin(); it != connections.end();)
    {
        it->outgoing.append(line, length);
        it->outgoing.push_back('\n');

        if (it->outgoing.size() > MaxOutgoingBytes)
        {
            std::fprintf(stderr, "meegramd: dropping a client %zu bytes behind\n", it->outgoing.size());
            it = closeConnection(it);
            continue;
        }

        if (flushOutgoing(*it))
            ++it;
        else
            it = closeConnection(it);
    }
}

// The binary a connecting peer has to be running, derived from this one's own location
// rather than hardcoded: installed, that resolves to /opt/meegram/bin/meegram next to
// /opt/meegram/bin/meegramd; in a build tree, to build-app/meegram next to
// build-app/meegramd. Empty if it cannot be determined, which fails every check closed.
std::string expectedPeerExecutable()
{
    char self[PATH_MAX];

    const ssize_t length = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0)
        return {};

    self[length] = '\0';

    const char *slash = std::strrchr(self, '/');
    if (!slash)
        return {};

    return std::string(self, static_cast<size_t>(slash - self)) + "/meegram";
}

// Whether a freshly accepted connection is the UI and not some other process on the
// device that noticed an open socket.
//
// Worth being precise about what this does and does not buy. The socket carries a
// logged-in Telegram session with no further authentication, so anything that reaches it
// can read every chat and send messages. SO_PEERCRED is kernel-supplied and unforgeable,
// so the uid check is solid, and the executable check stops a different application from
// simply connecting.
//
// It is not a security boundary against a determined attacker at the same uid, and
// nothing here can make it one: that attacker can ptrace the real meegram process and
// drive the socket from inside it, or skip the daemon entirely and read
// ~/.meegram/tdlib, which is unencrypted because nothing calls
// checkDatabaseEncryptionKey. This raises the bar from "any process can" to "any process
// that can already impersonate or subvert the UI can", which is the honest description.
bool isPeerTrusted(int fd, const std::string &expected)
{
    ucred credentials{};
    socklen_t length = sizeof(credentials);

    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) < 0)
    {
        std::perror("meegramd: SO_PEERCRED");
        return false;
    }

    if (credentials.uid != ::getuid())
    {
        std::fprintf(stderr, "meegramd: rejecting connection from uid %u\n", credentials.uid);
        return false;
    }

    if (expected.empty())
    {
        std::fprintf(stderr, "meegramd: cannot resolve own path, so no peer can be verified\n");
        return false;
    }

    // Read immediately. The pid is a snapshot taken at connect() time, so if that process
    // has since exited and its pid been recycled, this reads the wrong /proc entry. The
    // window is between accept() and here, and nothing else runs in it.
    char link[64];
    std::snprintf(link, sizeof(link), "/proc/%d/exe", credentials.pid);

    char peer[PATH_MAX];
    const ssize_t peerLength = ::readlink(link, peer, sizeof(peer) - 1);
    if (peerLength <= 0)
    {
        std::fprintf(stderr, "meegramd: cannot read %s; rejecting\n", link);
        return false;
    }

    peer[peerLength] = '\0';

    if (expected != peer)
    {
        std::fprintf(stderr, "meegramd: rejecting %s (expected %s)\n", peer, expected.c_str());
        return false;
    }

    return true;
}

// Refuses to start if a daemon already holds the socket, rather than unlinking it and
// silently stealing the connection from a running instance. A socket left behind by a
// crash refuses connect() with ECONNREFUSED and is safe to remove.
bool isSocketLive(const std::string &path)
{
    const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0)
        return false;

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    const bool live = ::connect(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    ::close(probe);

    return live;
}

int listenOn(const std::string &path)
{
    if (isSocketLive(path))
    {
        std::fprintf(stderr, "meegramd: already running on %s\n", path.c_str());
        return -1;
    }

    ::unlink(path.c_str());

    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0)
    {
        std::perror("meegramd: socket");
        return -1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        std::perror("meegramd: bind");
        ::close(listener);
        return -1;
    }

    // The socket carries a logged-in Telegram session. Owner-only, and set before any
    // connection can be accepted.
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);

    if (::listen(listener, 4) < 0)
    {
        std::perror("meegramd: listen");
        ::close(listener);
        return -1;
    }

    return listener;
}

// The name dbus-daemon activates this process under, matching
// resources/com.meegram.Daemon.service. Distinct from `com.meegram`, which the UI owns
// (NotificationManager.cpp) - the two are independent names despite the shared prefix.
constexpr auto BusName = "com.meegram.Daemon";

// Three outcomes, and they are not interchangeable: no bus means carry on without
// activation, an owned name means another meegramd is live and this one must not touch
// its socket.
enum class BusStatus
{
    Claimed,
    Unavailable,
    AlreadyRunning,
};

// Claims the activation name. Called *before* the socket is bound, because this is the
// real mutual exclusion between two meegramd processes: the unlink-then-bind in listenOn
// cannot arbitrate a genuine race, since both instances would see a dead socket and the
// second would unlink the first's and bind its own.
//
// The cost of that ordering is that the name appears slightly before the socket does, so
// a UI that connects the instant StartServiceByName returns can be too early. Client's
// connectToDaemon retries for exactly that window.
//
// A missing session bus is not fatal: meegramd is a socket relay first, and running it by
// hand over SSH with no DBUS_SESSION_BUS_ADDRESS is how it gets debugged. There the
// isSocketLive check is the only exclusion, which is enough for one developer.
//
// libdbus rather than QtDBus. Pulling QtCore into this process for one name registration
// would add megabytes to a daemon whose entire justification is a 24 MiB resident set
// against the UI's 78 MiB (docs/restructuring.md).
BusStatus claimBusName(DBusConnection **out)
{
    *out = nullptr;

    DBusError error;
    dbus_error_init(&error);

    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection)
    {
        std::fprintf(stderr, "meegramd: no session bus (%s); D-Bus activation unavailable\n",
                     dbus_error_is_set(&error) ? error.message : "unknown");
        dbus_error_free(&error);
        return BusStatus::Unavailable;
    }

    // libdbus calls exit() on disconnect by default, from inside dispatch, which would
    // skip the socket cleanup at the end of main and strand the file. The poll loop
    // notices the disconnect itself and unwinds properly.
    //
    // It does still exit on disconnect, just not abruptly: the session bus going away
    // means the session is ending, and a daemon that outlived it would hold the TDLib
    // database lock against the next login. Surviving the *UI* is the requirement here,
    // and that has nothing to do with the bus.
    dbus_connection_set_exit_on_disconnect(connection, FALSE);

    // DO_NOT_QUEUE: if another meegramd already owns the name, this one should lose and
    // exit, not wait in line for a name it would acquire only after the other died.
    const int result = dbus_bus_request_name(connection, BusName, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);

    if (dbus_error_is_set(&error))
    {
        std::fprintf(stderr, "meegramd: cannot claim %s: %s\n", BusName, error.message);
        dbus_error_free(&error);
        dbus_connection_unref(connection);
        return BusStatus::Unavailable;
    }

    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        std::fprintf(stderr, "meegramd: %s is already owned; exiting\n", BusName);
        dbus_connection_unref(connection);
        return BusStatus::AlreadyRunning;
    }

    *out = connection;

    return BusStatus::Claimed;
}

}  // namespace

int main(int argc, char *argv[])
{
    // A flag rather than an environment variable, deliberately. meegramd is D-Bus
    // activated, and any process at this uid can put variables into the session bus's
    // activation environment - so an env-var escape hatch could be switched on by the
    // very thing the peer check exists to keep out. The Exec line in
    // resources/com.meegram.Daemon.service does not pass this, so an activated daemon
    // always enforces; it is for running the relay by hand against socat.
    bool trustAnyPeer = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--trust-any-peer") == 0)
            trustAnyPeer = true;
    }

    const std::string expectedPeer = expectedPeerExecutable();

    if (trustAnyPeer)
        std::fprintf(stderr, "meegramd: --trust-any-peer, accepting any process at this uid\n");

    // The UI dying mid-write must not take the daemon with it. MSG_NOSIGNAL covers the
    // broadcast path; this covers everything else.
    ::signal(SIGPIPE, SIG_IGN);

    // Before any other libdbus call, which is what the API requires. Two threads reach
    // D-Bus here: the poll loop owns the bus name and dispatches taps, and the receive
    // thread posts notifications on its own private connection.
    dbus_threads_init_default();

    // Before the socket, deliberately - see claimBusName.
    DBusConnection *busConnection = nullptr;
    if (claimBusName(&busConnection) == BusStatus::AlreadyRunning)
        return 1;

    // libdbus multiplexes everything for a bus connection over one socket, so the simple
    // single-descriptor integration is enough here; the full watch/timeout callback dance
    // buys nothing for a connection that owns a name and answers no methods.
    int busFd = -1;
    if (busConnection && !dbus_connection_get_unix_fd(busConnection, &busFd))
        busFd = -1;

    const std::string path = socketPath();

    const int listener = listenOn(path);
    if (listener < 0)
        return 1;

    // Nothing is created until the first request is sent, so this is just an id.
    const int clientId = td_create_client_id();

    // Its constructor sends the option that turns TDLib's notifications on, so this also
    // instantiates the client.
    notifier = new Notifier([clientId](const std::string &request) { td_send(clientId, request.c_str()); });

    // Taps arrive on the connection that owns com.meegram.Daemon, and are dispatched by
    // the poll loop below.
    notifier->attachTapHandler(busConnection);

    // Receive on its own thread: td_receive blocks, and the poll loop below has to stay
    // responsive to new connections and inbound requests while it does.
    std::thread receiver([clientId] {
        // Instantiates the client, so td_receive has something to pump even before a UI
        // connects.
        //
        // The "@extra" is not decoration. A line without one is an update, and every UI
        // emits those to its subscribers - so an unmarked response to this would arrive
        // dressed as an update. TDLib echoes "@extra" back verbatim, and no UI will claim
        // this one: they match on a "<pid>-" prefix of their own.
        td_send(clientId, R"({"@type":"getOption","name":"version","@extra":"meegramd"})");

        // Empty until TDLib first says. See nudgeIfOffline: before that it has no
        // parameters and is not trying to connect, so there is nothing to prod.
        std::optional<bool> connected;

        for (;;)
        {
            const char *line = td_receive(30.0);

            if (line)
            {
                const size_t length = std::strlen(line);

                if (const auto state = connectionState(line, length))
                {
                    connected = *state;

                    // Verbatim, because the distinction is the diagnosis and there are
                    // only a handful of these in a session: WaitingForNetwork is the
                    // device having no route, Connecting is TDLib failing to reach
                    // Telegram over one it believes it has.
                    std::fprintf(stderr, "meegramd: %s\n", line);
                }

                // Relayed first. The notifier makes blocking D-Bus calls, and a UI waiting
                // on a chat list must not be behind a notification daemon that is thinking
                // about it. TDLib's buffer stays valid until the next td_receive, so it is
                // still ours to read afterwards.
                broadcast(line, length);

                notifier->onUpdate(line, length);
            }

            // On every trip, not only on the timeout: a client that is failing to connect
            // is not necessarily a quiet one, and the 30 seconds are counted here rather
            // than assumed from td_receive's.
            nudgeIfOffline(clientId, connected);
        }
    });
    receiver.detach();

    for (;;)
    {
        std::vector<pollfd> fds;
        size_t busIndex = 0;
        size_t firstConnection = 0;
        {
            const std::lock_guard<std::mutex> lock(connectionsMutex);

            fds.reserve(connections.size() + 2);
            fds.push_back(pollfd{listener, POLLIN, 0});

            if (busFd >= 0)
            {
                busIndex = fds.size();
                fds.push_back(pollfd{busFd, POLLIN, 0});
            }

            firstConnection = fds.size();

            for (const auto &connection : connections)
            {
                // POLLOUT only while there is something queued: asking for it
                // unconditionally would make poll() return immediately, every time.
                const short events = static_cast<short>(POLLIN | (connection.outgoing.empty() ? 0 : POLLOUT));
                fds.push_back(pollfd{connection.fd, events, 0});
            }
        }

        if (::poll(fds.data(), fds.size(), -1) < 0)
        {
            if (errno == EINTR)
                continue;

            std::perror("meegramd: poll");
            break;
        }

        // Nothing here answers method calls - the name is the whole point. Dispatching
        // anyway is not optional: unread messages accumulate in the bus connection, and a
        // connection that never drains eventually gets dropped by dbus-daemon, taking the
        // name with it. libdbus replies UnknownMethod on our behalf for anything that
        // does arrive.
        if (busIndex != 0 && (fds[busIndex].revents & POLLIN))
        {
            dbus_connection_read_write(busConnection, 0);
            while (dbus_connection_dispatch(busConnection) == DBUS_DISPATCH_DATA_REMAINS)
            {
            }

            // The session bus went away, so the session is ending. Unwind rather than
            // linger holding the TDLib database lock. See claimBusName.
            if (!dbus_connection_get_is_connected(busConnection))
            {
                std::fprintf(stderr, "meegramd: session bus closed; shutting down\n");
                break;
            }
        }

        if (fds[0].revents & POLLIN)
        {
            const int accepted = ::accept(listener, nullptr, nullptr);
            if (accepted >= 0 && !trustAnyPeer && !isPeerTrusted(accepted, expectedPeer))
            {
                // Closed rather than answered. The peer's connect() has already
                // succeeded, so it sees an immediate EOF; the reason is logged here,
                // which is the end that can do anything about it.
                ::close(accepted);
            }
            else if (accepted >= 0)
            {
                // Non-blocking, so broadcast can never park inside a write. See the note
                // on Connection::outgoing - this is the half of the deadlock fix that
                // makes the queue work.
                ::fcntl(accepted, F_SETFL, ::fcntl(accepted, F_GETFL, 0) | O_NONBLOCK);

                const std::lock_guard<std::mutex> lock(connectionsMutex);

                connections.push_back(Connection{accepted, std::string(), std::string()});
            }
        }

        // Requests the notifier wants to see, handed to it after connectionsMutex is
        // released. It posts notifications under a lock of its own and a blocked
        // notification daemon can hold that for a couple of seconds - which, taken while
        // holding this one, would stall the broadcast thread behind it.
        std::vector<std::string> peeked;

        for (size_t i = firstConnection; i < fds.size(); ++i)
        {
            if (fds[i].revents == 0)
                continue;

            // Looked up by descriptor, not by index: the broadcast thread may have
            // dropped a dead connection since fds was built, which shifts every index
            // after it.
            const std::lock_guard<std::mutex> lock(connectionsMutex);

            const auto it = std::find_if(connections.begin(), connections.end(),
                                         [&fds, i](const Connection &c) { return c.fd == fds[i].fd; });
            if (it == connections.end())
                continue;

            // Drain the backlog first. This is the half of the deadlock fix that actually
            // makes progress: broadcast only ever queues, so without this the queue would
            // never empty.
            if ((fds[i].revents & POLLOUT) && !flushOutgoing(*it))
            {
                closeConnection(it);
                continue;
            }

            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            char buffer[8192];
            const ssize_t n = ::read(it->fd, buffer, sizeof(buffer));

            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;

            if (n <= 0)
            {
                closeConnection(it);
                continue;
            }

            it->pending.append(buffer, static_cast<size_t>(n));

            for (size_t newline; (newline = it->pending.find('\n')) != std::string::npos;)
            {
                std::string request = it->pending.substr(0, newline);
                it->pending.erase(0, newline + 1);

                if (request.empty())
                    continue;

                td_send(clientId, request.c_str());

                // Peeked, not intercepted: openChat and closeChat are how this process
                // knows which chat the user is looking at, which is the one thing about
                // the UI that the update stream does not say.
                peeked.push_back(std::move(request));
            }
        }

        for (const auto &request : peeked)
            notifier->onRequest(request);
    }

    ::close(listener);
    ::unlink(path.c_str());

    return 0;
}
