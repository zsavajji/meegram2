#include "Client.hpp"

#include <mutex>

Client::Client(QObject *parent)
    : QObject(parent)
    , m_clientManager(std::make_unique<td::ClientManager>())
{
    // disable TDLib logging
    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1));

    m_clientId = m_clientManager->create_client_id();

    initialize();
}

// Out of line only because the header now declares it - ClientProxy needs a real one to
// unblock its reader, and the two share a declaration. Behaviour is the implicit
// destructor's, unchanged: ~jthread requests a stop and joins, which takes effect when
// the receive(30.0) below next returns.
Client::~Client() = default;

int Client::clientId() const noexcept
{
    return m_clientId;
}

void Client::send(td::td_api::object_ptr<td::td_api::Function> request, std::function<void(td::td_api::object_ptr<td::td_api::Object>)> callback)
{
    auto id = m_requestId.fetch_add(1, std::memory_order_relaxed);
    if (callback)
    {
        std::unique_lock lock(m_handlerMutex);

        m_handlers.emplace(id, std::move(callback));
    }
    m_clientManager->send(m_clientId, id, std::move(request));
}

bool Client::reconnect()
{
    // Nothing to reconnect: TDLib is in this process, its worker has not gone anywhere,
    // and a socket that does not exist cannot have died. True rather than false so the
    // caller carries on to re-run initialize(), which is the only half of a retry that
    // means anything here - and TDLib answering "Unexpected setTdlibParameters" to the
    // second attempt is already handled as success (AppManager::setParameters).
    return true;
}

QString Client::lastConnectError() const
{
    // Nothing to connect to and so nothing to fail: TDLib is in this process.
    return QString();
}

void Client::initialize()
{
    // A worker thread using std::jthread for automatic joining
    m_worker = std::jthread([this](std::stop_token token) {
        while (!token.stop_requested())
        {
            auto response = m_clientManager->receive(30.0);
            if (!response.object)
            {
                continue;
            }

            if (response.request_id != 0)
            {
                std::function<void(td::td_api::object_ptr<td::td_api::Object>)> handler;
                {
                    std::shared_lock lock(m_handlerMutex);
                    auto it = m_handlers.find(response.request_id);
                    if (it != m_handlers.end())
                    {
                        handler = std::move(it->second);
                    }
                }

                if (handler)
                {
                    handler(std::move(response.object));
                    {
                        std::unique_lock lock(m_handlerMutex);
                        m_handlers.erase(response.request_id);
                    }
                }
            }
            else
            {
                // Handed to the GUI thread whole. Emitting result() from here would cross
                // the thread boundary once per subscriber - four posted invocations - and
                // need a fifth queued call to free the object after they drained. One
                // posted call does the emit over there and frees it on the way out; see
                // dispatch().
                QMetaObject::invokeMethod(this, "dispatch", Qt::QueuedConnection, Q_ARG(void *, response.object.release()));
            }
        }
    });
}

void Client::dispatch(void *pointer)
{
    // Owned for the length of this call and no longer. Every receiver of result() lives on
    // this thread, so the emit below runs them all synchronously and in connection order;
    // when the last one returns, nothing may still be holding the object. Handlers move the
    // fields they need out of the update and never keep the shell, which is what makes that
    // true. td::td_api::Object derives from td::TlObject, which has a virtual destructor.
    //
    // One queued event per update rather than one per subscriber plus a disposal behind
    // them: emitting across the thread boundary turned into four posted invocations and a
    // fifth queued call to free the object after they drained. Five event-loop trips per
    // update is what a 2000-update startup replay spent its GUI-thread time on.
    const std::unique_ptr<td::td_api::Object> object(static_cast<td::td_api::Object *>(pointer));

    emit result(object.get());
}
