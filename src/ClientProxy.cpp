// The socket implementation of Client. Selected by MEEGRAM_JSON_TRANSPORT; see Client.hpp.
//
// Same public surface as src/Client.cpp, and deliberately the same *semantics*, including
// two that are load-bearing and undocumented at every call site:
//
//   - result() is emitted on the GUI thread from dispatch(), one posted call per update,
//     and the object is freed when the last subscriber returns. Subscribers hold the raw
//     pointer for the length of their slot and none may delete it. The argument is
//     spelled out in Client.cpp and is reproduced here because it survives the process
//     split unchanged.
//
//   - send() callbacks run on the reader thread, exactly as they do today
//     (docs/architecture.md, "Threading"). Four call sites touch model state from there -
//     AppManager.cpp:342 and :360, LanguagePackInfoModel.cpp:79, MessageModel.cpp:194.
//     That is a pre-existing bug; reproducing it is correct here, because fixing it in
//     the same change as the transport would hide which of the two broke something.
//
// The wire is one JSON object per line, td_api objects encoded with the client-direction
// codec from src/JsonCodec.hpp.

#include "Client.hpp"

#include "JsonCodec.hpp"
#include "ScopeTimer.hpp"

#include "td/utils/JsonBuilder.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>

namespace {

// Must agree with socketPath() in src/daemon/main.cpp.
std::string socketPath()
{
    if (const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir)
        return std::string(runtimeDir) + "/meegram.sock";

    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.meegram/sock";
}

// Matches resources/com.meegram.Daemon.service and BusName in src/daemon/main.cpp.
// Distinct from `com.meegram`, which this process owns via NotificationManager.
constexpr auto DaemonService = "com.meegram.Daemon";

// Why the last connect failed, in one line, for the screen that has to explain itself.
//
// Every branch below already says this to qWarning, and on this device that is a file no
// user can read: the launcher runs the app through invoker, which discards stderr, so the
// only copy is in ~/.meegram/meegram.log on a phone whose owner is the only person who can
// see the failure. The four failures are not interchangeable - a bus that would not
// activate meegramd, a meegramd that never bound its socket, and a socket that was there
// all along are three different bugs - and the screen used to flatten all of them into
// "Nothing has come back since startup".
//
// A static because the connect happens in Client's member initialiser, before there is a
// Client to hang it on. Written only from connectToDaemon, which runs on the UI thread.
QString &connectError()
{
    static QString reason;
    return reason;
}

// Whether meegramd hung up on its own since the last connect. Cleared by every attempt,
// set by the reader.
//
// Separate from the string above, and an atomic rather than a second QString, because the
// reader thread writes this one while the UI thread reads it - and all it has to carry is
// which of two stories to tell. It matters because the two are indistinguishable from the
// connect alone: a daemon that accepts and then closes gives a connect() that succeeded,
// so the screen reported "meegramd answered, but TDLib behind it did not" for a daemon
// that had hung up on the app 5461 times.
std::atomic<bool> &daemonDropped()
{
    static std::atomic<bool> dropped{false};
    return dropped;
}

// One attempt, no diagnostics - the caller decides whether a failure is worth reporting,
// because the first one never is.
int tryConnect(const std::string &path)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        qWarning("Client: socket() failed: %s", std::strerror(errno));
        return -1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        ::close(fd);
        return -1;
    }

    return fd;
}

// Asks dbus-daemon to activate meegramd, via resources/com.meegram.Daemon.service.
bool startDaemonService()
{
    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    if (!interface)
    {
        qWarning("Client: no session bus, so meegramd cannot be activated. Start it by hand.");
        connectError() = QLatin1String("No session bus, so meegramd could not be started.");
        return false;
    }

    // startService on an already-owned name is a no-op that reports success, so this is
    // only to keep the common case off the bus entirely.
    if (interface->isServiceRegistered(QLatin1String(DaemonService)).value())
        return true;

    // Not const: Qt 4's QDBusReply::error() is a non-const member.
    QDBusReply<void> reply = interface->startService(QLatin1String(DaemonService));
    if (!reply.isValid())
    {
        qWarning("Client: cannot activate %s: %s", DaemonService, qPrintable(reply.error().message()));

        // Verbatim, because the distinction is the diagnosis. A name dbus-daemon does not
        // know reads as ServiceUnknown and means the package is half installed; a meegramd
        // that exits before it claims the name reads as NoReply, twenty-five seconds later,
        // and means meegramd itself failed - its own log says why.
        connectError() = QString::fromLatin1("meegramd would not start: ") + reply.error().message();
        return false;
    }

    return true;
}

int connectToDaemon()
{
    const std::string path = socketPath();

    // Cleared here rather than at each success: this is the one entry point, and a reconnect
    // that works must not leave the previous failure on screen.
    connectError().clear();
    daemonDropped() = false;

    MEEGRAM_MARK("daemon-connect-begin");

    // Already running, which is the whole point of the daemon - this is the path taken
    // every time the UI is reopened against a connection that never went away.
    if (const int fd = tryConnect(path); fd >= 0)
    {
        // A warm start ends here, and the whole block below is what a cold one pays
        // instead. Two runs of the same log tell the two apart on this one line.
        MEEGRAM_MARK("daemon-already-up");
        return fd;
    }

    MEEGRAM_MARK("daemon-activate-begin");

    if (!startDaemonService())
        return -1;

    // StartServiceByName is synchronous and dbus-daemon does not answer until meegramd
    // owns the name, so this marker has the whole fork+exec of a 49 MB binary behind it,
    // plus everything meegramd runs before claimBusName.
    MEEGRAM_MARK("daemon-activated");

    // meegramd claims the bus name before it binds the socket, deliberately - that
    // ordering is what makes the name a real mutex between two daemons (see claimBusName
    // in src/daemon/main.cpp). The cost is this window: StartServiceByName has returned,
    // so the name exists, but the listen() may be a few milliseconds behind it.
    //
    // Polling rather than waiting on a signal because this runs in AppManager's
    // constructor, before there is an event loop to deliver one. 3 seconds is far longer
    // than a local exec needs and still short enough not to look like a hang.
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        ::usleep(20 * 1000);

        if (const int fd = tryConnect(path); fd >= 0)
        {
            // The attempt count is the number that matters here, not the elapsed time:
            // it says whether the socket was one poll behind the bus name or a hundred,
            // which is the difference between "20 ms of slop" and "the listen() is
            // waiting on something slow in meegramd".
            qWarning("Client: daemon socket appeared after %d polls (%d ms)", attempt + 1, (attempt + 1) * 20);
            MEEGRAM_MARK("daemon-socket-up");
            return fd;
        }
    }

    qWarning("Client: activated %s but no socket on %s: %s", DaemonService, path.c_str(), std::strerror(errno));

    connectError() = QString::fromLatin1("meegramd started but never opened ") + QString::fromStdString(path);

    return -1;
}

}  // namespace

Client::Client(QObject *parent)
    : QObject(parent)
    , m_clientId(0)
    , m_socket(connectToDaemon())
    , m_extraPrefix(std::to_string(::getpid()) + "-")
{
    // Deliberately not ClientManager::execute(setLogVerbosityLevel) as the native path
    // does: TDLib lives in meegramd now, and its logging is meegramd's to configure.

    if (m_socket >= 0)
        initialize();
}

Client::~Client()
{
    // Before the shutdown, not after. The reader treats a read that ends while the token is
    // unset as meegramd going away and emits disconnected() - so the stop request is the
    // only thing that tells a socket this process closed from one that closed on it.
    m_worker.request_stop();

    // The reader is parked in read(). A stop_token cannot interrupt that, so the socket has
    // to be shut down as well: read() returns 0 and the loop falls out on its own. Without
    // this ~jthread would join a thread that is never going to wake.
    if (m_socket >= 0)
        ::shutdown(m_socket, SHUT_RDWR);

    if (m_worker.joinable())
        m_worker.join();

    if (m_socket >= 0)
        ::close(m_socket);
}

QString Client::lastConnectError() const
{
    if (!connectError().isEmpty())
        return connectError();

    // The connect succeeded and the daemon hung up anyway, which is what a peer meegramd
    // will not accept looks like from this side - and is not the same failure as a socket
    // that works with nothing behind it.
    if (daemonDropped())
        return QLatin1String("meegramd closed the connection. Its own log says why.");

    return QString();
}

int Client::clientId() const noexcept
{
    // TDLib's client id belongs to meegramd and is meaningless on this side of the
    // socket. Nothing in src/ reads this - it is kept only so the two implementations
    // present the same surface.
    return m_clientId;
}

void Client::send(td::td_api::object_ptr<td::td_api::Function> request, std::function<void(td::td_api::object_ptr<td::td_api::Object>)> callback)
{
    // No daemon: the constructor already said so once. Dropping the request here rather
    // than encoding it and failing at the write keeps that to a single message instead of
    // one per call for the lifetime of the process.
    if (m_socket < 0)
        return;

    auto id = m_requestId.fetch_add(1, std::memory_order_relaxed);
    if (callback)
    {
        std::unique_lock lock(m_handlerMutex);

        m_handlers.emplace(id, std::move(callback));
    }

    auto request_json = td::json_encode<std::string>(td::ToJson(request));

    // Appending before the closing brace, which is how TDLib itself attaches @extra
    // (ClientJson.cpp:from_response). to_json always writes "@type" first, so the object
    // is never empty and the leading comma always has a field in front of it.
    if (request_json.empty() || request_json.back() != '}')
    {
        qWarning("Client: refusing to send malformed request encoding");
        return;
    }

    request_json.pop_back();

    // "@extra" is namespaced by process id, not just the request counter. meegramd
    // broadcasts every line to every connection rather than keeping a routing table, so
    // two UIs would otherwise both answer to "7". A foreign response now fails the prefix
    // check below and is dropped, which is what today's Client does with an unmatched
    // request id.
    request_json += ",\"@extra\":\"" + m_extraPrefix + std::to_string(id) + "\"}\n";

    const std::lock_guard<std::mutex> lock(m_writeMutex);

    for (size_t written = 0; written < request_json.size();)
    {
        const ssize_t n = ::send(m_socket, request_json.data() + written, request_json.size() - written, MSG_NOSIGNAL);
        if (n > 0)
        {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;

        qWarning("Client: write to meegramd failed: %s", std::strerror(errno));
        return;
    }
}

bool Client::reconnect()
{
    // The destructor's sequence, plus an open at the end - and the order is the whole
    // function. The stop is requested first so the reader does not report a socket this
    // function is replacing as one meegramd dropped; the reader is parked in read() on
    // m_socket and a stop_token cannot interrupt that, so the socket is shut down as well;
    // and the old thread has to be joined before m_socket is reassigned, or it wakes up
    // reading the connection that replaced it.
    m_worker.request_stop();

    if (m_socket >= 0)
        ::shutdown(m_socket, SHUT_RDWR);

    if (m_worker.joinable())
        m_worker.join();

    if (m_socket >= 0)
    {
        ::close(m_socket);
        m_socket = -1;
    }

    // Whatever was in flight died with the connection. Request ids never repeat within a
    // process, so a stale one cannot match a handler registered after this - what the
    // clear is for is the closures themselves, which would otherwise hold what they
    // captured for the life of the app waiting for an answer that was lost with the
    // socket.
    {
        std::unique_lock lock(m_handlerMutex);

        m_handlers.clear();
    }

    // ponytail: blocks the UI thread for up to three seconds when the daemon has to be
    // activated, the same wait the constructor already takes at startup. A thread and a
    // signal if that ever feels broken under a finger rather than at launch.
    m_socket = connectToDaemon();

    if (m_socket < 0)
        return false;

    initialize();

    return true;
}

void Client::initialize()
{
    m_worker = std::jthread([this](std::stop_token token) {
        std::string buffered;
        char buffer[8192];

        // How far into `buffered` the newline search has already been. A line that does
        // not fit in one read - the language pack is 1.8 MB on one line - used to be
        // searched from its first byte again on every 8 KB that arrived: a scan over the
        // whole partial line per read, quadratic in the line's length.
        size_t scanned = 0;

        while (!token.stop_requested())
        {
            const ssize_t n = ::read(m_socket, buffer, sizeof(buffer));
            if (n == 0)
                break;
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                if (!token.stop_requested())
                    qWarning("Client: read from meegramd failed: %s", std::strerror(errno));
                break;
            }

            buffered.append(buffer, static_cast<size_t>(n));

            // Consumed up to here. Lines are cut out in place and the buffer is trimmed
            // once per read rather than once per line.
            size_t consumed = 0;

            for (size_t newline; (newline = buffered.find('\n', scanned)) != std::string::npos;)
            {
                if (newline > consumed)
                {
                    std::string line = buffered.substr(consumed, newline - consumed);
                    handleLine(line);
                }

                consumed = newline + 1;
                scanned = consumed;
            }

            buffered.erase(0, consumed);

            // Whatever is left has been searched and holds no newline, so the next read
            // only has to look at what it appends.
            scanned = buffered.size();
        }

        // A stop that was asked for is this process closing its own socket - the destructor
        // and reconnect() both request it before they shut the socket down. Anything else is
        // meegramd going away under a running app, and nothing used to notice: send() drops
        // every request before it is encoded once the socket is dead, so the UI sat on a
        // connection that could not answer for the rest of the run, with no error and no way
        // back. AppManager::handleDaemonGone reopens it.
        //
        // Queued by Qt, because this is the reader thread and every receiver is on the main
        // one.
        if (!token.stop_requested())
        {
            qWarning("Client: meegramd closed the connection");

            daemonDropped() = true;

            emit disconnected();
        }
    });
}

void Client::handleLine(std::string &line)
{
    // json_decode unescapes in place, so it needs a mutable buffer it is allowed to
    // scribble on - hence the non-const reference and the copy the caller already made.
    auto r_value = td::json_decode(td::MutableSlice(line));
    if (r_value.is_error())
    {
        qWarning("Client: undecodable line from meegramd: %s", r_value.error().message().str().c_str());
        return;
    }

    auto value = r_value.move_as_ok();
    if (value.type() != td::JsonValue::Type::Object)
        return;

    std::uint64_t requestId = 0;
    bool isResponse = false;

    if (value.get_object().has_field("@extra"))
    {
        isResponse = true;

        // extract_field rather than a peek: it is the public accessor, and dropping the
        // field on the way past keeps it out of the object from_json then walks.
        auto extra = value.get_object().extract_field("@extra");
        if (extra.type() != td::JsonValue::Type::String)
            return;

        const std::string text = extra.get_string().str();

        // Ours only if it carries this process's prefix. Anything else belongs to another
        // UI on the same daemon.
        if (text.size() <= m_extraPrefix.size() || text.compare(0, m_extraPrefix.size(), m_extraPrefix) != 0)
            return;

        requestId = std::strtoull(text.c_str() + m_extraPrefix.size(), nullptr, 10);
    }

    td::td_api::object_ptr<td::td_api::Object> object;
    if (auto status = td::td_api::from_json(object, std::move(value)); status.is_error())
    {
        qWarning("Client: undecodable object from meegramd: %s", status.message().str().c_str());
        return;
    }
    if (!object)
        return;

    if (isResponse)
    {
        // Responses never reach result(), matching the native path: there, a non-zero
        // request_id with no registered handler simply drops the object.
        std::function<void(td::td_api::object_ptr<td::td_api::Object>)> handler;
        {
            std::shared_lock lock(m_handlerMutex);
            auto it = m_handlers.find(requestId);
            if (it != m_handlers.end())
            {
                handler = std::move(it->second);
            }
        }

        if (handler)
        {
            // On the reader thread, as on the native path. See the note at the top.
            handler(std::move(object));
            {
                std::unique_lock lock(m_handlerMutex);
                m_handlers.erase(requestId);
            }
        }

        return;
    }

    // Handed to the GUI thread whole, same as Client.cpp and for the same reason: one
    // posted call per update, the emit and the free both over there. See dispatch().
    QMetaObject::invokeMethod(this, "dispatch", Qt::QueuedConnection, Q_ARG(void *, object.release()));
}

void Client::dispatch(void *pointer)
{
    // Duplicated from Client.cpp rather than shared: a few lines, against a third
    // translation unit for the two implementations to agree on. The reasoning is there.
    const std::unique_ptr<td::td_api::Object> object(static_cast<td::td_api::Object *>(pointer));

    emit result(object.get());
}
