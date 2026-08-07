// The socket implementation of Client. Selected by MEEGRAM_JSON_TRANSPORT; see Client.hpp.
//
// Same public surface as src/Client.cpp, and deliberately the same *semantics*, including
// two that are load-bearing and undocumented at every call site:
//
//   - result() is emitted queued and the disposal is queued behind it. Four subscribers
//     hold the raw pointer and none may delete it. The argument is spelled out in
//     Client.cpp and is reproduced here because it survives the process split unchanged.
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
        return false;
    }

    return true;
}

int connectToDaemon()
{
    const std::string path = socketPath();

    // Already running, which is the whole point of the daemon - this is the path taken
    // every time the UI is reopened against a connection that never went away.
    if (const int fd = tryConnect(path); fd >= 0)
        return fd;

    if (!startDaemonService())
        return -1;

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
            return fd;
    }

    qWarning("Client: activated %s but no socket on %s: %s", DaemonService, path.c_str(), std::strerror(errno));

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
    // The reader is parked in read(). A stop_token cannot interrupt that, so shut the
    // socket down first: read() returns 0 and the loop falls out on its own. Without this
    // ~jthread would join a thread that is never going to wake.
    if (m_socket >= 0)
        ::shutdown(m_socket, SHUT_RDWR);

    m_worker.request_stop();

    if (m_worker.joinable())
        m_worker.join();

    if (m_socket >= 0)
        ::close(m_socket);
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
    // function. The reader is parked in read() on m_socket and a stop_token cannot
    // interrupt that, so the socket has to be shut down before the join; and the old
    // thread has to be joined before m_socket is reassigned, or it wakes up reading the
    // connection that replaced it.
    if (m_socket >= 0)
        ::shutdown(m_socket, SHUT_RDWR);

    m_worker.request_stop();

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

        while (!token.stop_requested())
        {
            const ssize_t n = ::read(m_socket, buffer, sizeof(buffer));
            if (n == 0)
            {
                qWarning("Client: meegramd closed the connection");
                break;
            }
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                qWarning("Client: read from meegramd failed: %s", std::strerror(errno));
                break;
            }

            buffered.append(buffer, static_cast<size_t>(n));

            for (size_t newline; (newline = buffered.find('\n')) != std::string::npos;)
            {
                std::string line = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);

                if (!line.empty())
                    handleLine(line);
            }
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

    // Ownership note, carried over from Client.cpp verbatim because the reasoning is
    // unchanged: result() is emitted from this worker thread to receivers that all live
    // on the main thread, so every connection is queued. Releasing the pointer here would
    // leak the update outright - no receiver can delete it, because the others still hold
    // it.
    //
    // Qt appends queued invocations to the receiving thread's event queue in order, so a
    // disposal posted after the emit is processed after all of result()'s slot
    // invocations have run. The handlers move the fields they need out of the update and
    // never retain the object itself, so freeing the shell at that point is safe.
    auto *raw = object.release();

    emit result(raw);

    QMetaObject::invokeMethod(this, "disposeObject", Qt::QueuedConnection, Q_ARG(void *, raw));
}

void Client::disposeObject(void *object)
{
    // td::td_api::Object derives from td::TlObject, which has a virtual destructor.
    // Duplicated from Client.cpp: three lines, against a second translation unit and a
    // shared header for the two implementations to agree on.
    delete static_cast<td::td_api::Object *>(object);
}
