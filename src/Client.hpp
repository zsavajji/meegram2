#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

// Two implementations, one public surface, selected by MEEGRAM_JSON_TRANSPORT in
// CMakeLists.txt:
//
//   Client.cpp       td::ClientManager in this process - what has always shipped
//   ClientProxy.cpp  a socket to meegramd, td_api objects carried as JSON
//
// Both paths stay buildable so a regression is one CMake flag away from being bisected.
// The surface below is what StorageManager, ChatModel, MessageModel, Authorization and
// CountryModel compile against, and it does not change between them.
class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    int clientId() const noexcept;

    void send(td::td_api::object_ptr<td::td_api::Function> request, std::function<void(td::td_api::object_ptr<td::td_api::Object>)> callback = {});

    // Drops the connection and opens a new one, then restarts the reader. False when there
    // is still nothing to connect to, in which case nothing has changed and it can be
    // called again. In process there is nothing to reconnect and it answers true - see the
    // note on the definition in Client.cpp.
    //
    // Called from AppManager::retry, which is the only thing in the app that can get a
    // dead transport moving again: with the socket gone, Client::send drops every request
    // before it is encoded, so no amount of re-sending reaches anyone.
    bool reconnect();

    // Why the last attempt to reach meegramd failed, or empty if it did not. Read by the
    // screen that reports the failure; see connectError() in ClientProxy.cpp. Always empty
    // on the in-process transport, where there is nothing to reach.
    QString lastConnectError() const;

    // Feeds a locally built object into result() as if TDLib had sent it.
    //
    // TDLib emits updateAuthorizationState only when the state *changes*. In process that
    // is invisible, because every launch starts a fresh client that walks the states from
    // the beginning. Against meegramd it is not: the daemon can already be authorized, so
    // a newly attached UI is told nothing and sits on the login page in front of a live
    // session. AppManager::requestAuthorizationState asks outright and replays the answer
    // through here, so every subscriber handles it on its normal path.
    //
    // Defined here rather than twice: the body is identical for both transports, and it
    // is the same ownership discipline a real update gets - queued emit, disposal queued
    // behind it. See the argument in Client::initialize.
    void injectUpdate(td::td_api::object_ptr<td::td_api::Object> object)
    {
        auto *raw = object.release();

        emit result(raw);

        QMetaObject::invokeMethod(this, "disposeObject", Qt::QueuedConnection, Q_ARG(void *, raw));
    }

signals:
    void result(td::td_api::Object *object);

    // meegramd went away under a running app: the reader lost the socket without this
    // process having asked it to. Emitted from the reader thread, so every connection to it
    // is queued.
    //
    // It matters because nothing else can notice. Client::send drops every request before
    // it is encoded once the socket is dead, so a UI that loses the daemon mid-run - the
    // session bus going away under it (see the poll loop in src/daemon/main.cpp), a daemon
    // killed by an upgrade, a client dropped for falling too far behind - goes quiet with
    // no error anywhere the user can see it. The stall deadline in AppManager only ever
    // fires once, seconds into startup, so after that there was no way back at all.
    //
    // Never emitted by the in-process transport, where there is no connection to lose.
    void disconnected();

private slots:
    // Frees an update after every queued result() slot invocation has run. See the
    // ordering argument in Client::initialize().
    void disposeObject(void *object);

private:
    void initialize();

    int m_clientId;

#ifdef MEEGRAM_JSON_TRANSPORT
    // Decodes one line off the socket and either invokes a handler or emits result().
    // Takes a mutable reference because json_decode unescapes in place.
    void handleLine(std::string &line);

    // The connection to meegramd. Written by send() from any thread, read by the worker.
    int m_socket;
    std::mutex m_writeMutex;

    // "<pid>-", prepended to every "@extra" so responses meant for another UI on the same
    // daemon can be told apart from this one's. See the note in ClientProxy::send.
    std::string m_extraPrefix;
#else
    std::unique_ptr<td::ClientManager> m_clientManager;
#endif

    std::jthread m_worker;
    std::shared_mutex m_handlerMutex;
    std::atomic<std::uint64_t> m_requestId{0};
    std::unordered_map<std::uint64_t, std::function<void(td::td_api::object_ptr<td::td_api::Object>)>> m_handlers;
};
