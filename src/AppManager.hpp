#pragma once

#include "LanguagePackInfoModel.hpp"
// Included rather than forward declared: unique_ptr needs the complete type wherever
// AppManager's implicit destructor is instantiated.
//
// Two of them, one per transport. With meegramd in the picture the notifications are
// posted there and the app keeps only the tap endpoint; without it, nothing else is
// resident, so the app still composes and posts them itself.
#ifdef MEEGRAM_JSON_TRANSPORT
#include "NotificationEndpoint.hpp"
#else
#include "NotificationManager.hpp"
#endif

#include <td/telegram/td_api.h>

#include <QObject>

#include <array>
#include <atomic>
#include <memory>

class Authorization;
class ChatManager;
class Client;
class Locale;
class StorageManager;
class Settings;

class AppManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool authorized READ isAuthorized NOTIFY authorizedChanged)
    Q_PROPERTY(QString connectionStateString READ connectionStateString NOTIFY connectionStateChanged)

    // TDLib never answered, so the startup spinner has nothing to resolve to. Distinct
    // from connectionStateString, which is TDLib reporting on its own connection - this
    // is not having heard from TDLib at all. See reportInitializationStall.
    Q_PROPERTY(bool serviceUnreachable READ isServiceUnreachable NOTIFY serviceUnreachableChanged)

    // All built in the constructor and never replaced, so no change to notify.
    Q_PROPERTY(Client *client READ client CONSTANT)
    Q_PROPERTY(Authorization *authorization READ authorization CONSTANT)
    Q_PROPERTY(Locale *locale READ locale CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(StorageManager *storageManager READ storageManager CONSTANT)

    Q_PROPERTY(ChatManager *chatManager READ chatManager NOTIFY chatManagerChanged)

    Q_PROPERTY(LanguagePackInfoModel *languagePackInfoModel READ languagePackInfoModel NOTIFY languagePackInfoModelChanged)
public:
    explicit AppManager(QObject *parent = nullptr);

    bool isAuthorized() const noexcept;

    bool isServiceUnreachable() const noexcept;

    const QString &connectionStateString() const noexcept;

    Client *client() const noexcept;
    Authorization *authorization() const noexcept;
    Locale *locale() const noexcept;
    Settings *settings() const noexcept;
    StorageManager *storageManager() const noexcept;

    ChatManager *chatManager() const noexcept;

    LanguagePackInfoModel *languagePackInfoModel() const noexcept;

signals:
    void chatManagerChanged();
    void languagePackInfoModelChanged();

    void authorizedChanged();

    void serviceUnreachableChanged();

    void connectionStateChanged();

    void appInitialized();

    // Forwarded from NotificationManager: a system notification was tapped.
    // Relayed straight from NotificationManager; a decimal string, see the note there.
    void chatRequested(const QString &chatId);

public slots:
    void close() noexcept;
    void setOption(const QString &name, const QVariant &value);
    void downloadFile(int fileId, int priority, qlonglong offset, qlonglong limit, bool synchronous);

    void initialize() noexcept;

    // The "Try again" button on MainPage's unreachable screen. Reopens the connection and
    // runs startup again from the top; does nothing but say so if there is still no daemon
    // to reach, so it can be pressed as many times as it takes.
    void retry() noexcept;

private slots:
    void handleResult(td::td_api::Object *object);

    void loadLanguagePack() noexcept;

    // Re-arms loadLanguagePack after a failed attempt. A slot because the failure is
    // noticed on the reader thread and a timer has to be started on this one.
    void scheduleLanguagePackRetry() noexcept;

    // Fires once, a few seconds in. Silent unless startup is incomplete, in which case it
    // names the half that is missing and releases the UI from waiting on the language
    // pack - the only half the app can do without.
    void reportInitializationStall() noexcept;

private:
    void setParameters() noexcept;

    // Queries the current authorization state and replays it as an update, because TDLib
    // only announces changes. See Client::injectUpdate.
    void requestAuthorizationState() noexcept;

    // The same problem, for everything else TDLib announces exactly once. Only built under
    // the daemon transport, which is the only one where "exactly once" can land in a
    // process that has already exited. See the note in AppManager.cpp.
#ifdef MEEGRAM_JSON_TRANSPORT
    void restoreState() noexcept;
#endif

    void checkInitializationStatus() noexcept;

    void handleAuthorizationState(const td::td_api::AuthorizationState &authorizationState);
    void handleConnectionState(const td::td_api::ConnectionState &connectionState);

    bool m_isAuthorized{false};

    // One way. Set at the stall deadline and never cleared: there is no reconnect path -
    // the socket is opened once, in Client's constructor - so a transport that is dead at
    // eight seconds is dead for the run. If TDLib does answer late, appInitialized fires
    // and MainPage leaves this state on `initialized` without consulting it.
    bool m_serviceUnreachable{false};

    QString m_connectionStateString;

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Authorization> m_authorization;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<Settings> m_settings;

    std::shared_ptr<ChatManager> m_chatManager;
    std::shared_ptr<StorageManager> m_storageManager;

    std::unique_ptr<LanguagePackInfoModel> m_languagePackInfoModel;

    // Not exposed to QML. Under the daemon transport it is a D-Bus endpoint and nothing
    // more, built with the rest of AppManager; otherwise it is the in-process notifier,
    // which needs a StorageManager and so waits for authorization.
#ifdef MEEGRAM_JSON_TRANSPORT
    std::unique_ptr<NotificationEndpoint> m_notificationEndpoint;
#else
    std::unique_ptr<NotificationManager> m_notificationManager;
#endif

    // [0] setTdlibParameters accepted. [1] startup is done waiting on the language pack -
    // either it arrived or reportInitializationStall gave up on it. Both true is what
    // appInitialized means, and [1] is deliberately not "the pack is loaded": the retry
    // outlives the deadline.
    //
    // Atomic because the two halves are now set from different threads - [0] and a loaded
    // [1] from a send() callback on the reader thread, the deadline's [1] from a timer on
    // this one. Two plain writes racing there can leave each side reading the other's flag
    // as still false, and the missed emit is a spinner that never resolves, which is the
    // bug this pair exists to prevent.
    std::array<std::atomic<bool>, 2> m_initializationStatus{};
};
