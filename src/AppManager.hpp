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

    // Whether to offer the sign-in screen. Distinct from !authorized, which is also true
    // before TDLib has said anything - and MainPage used !chatManager for this, so the
    // moment appInitialized fired ahead of authorizationStateReady a signed-in user was
    // shown the "StartMessaging" welcome screen. Those two are independent: appInitialized
    // needs setTdlibParameters plus the language pack, both of which a warm daemon answers
    // at once, while the authorization state still costs a round trip.
    //
    // Seeded from Settings::wasAuthorized so it is already right before TDLib answers, and
    // corrected by handleAuthorizationState the moment it does. That makes the common case
    // - a signed-in user reopening the app - show the spinner rather than a screen telling
    // them to sign in, and a genuinely signed-out user reach the sign-in button with no
    // round trip at all.
    Q_PROPERTY(bool signedOut READ isSignedOut NOTIFY signedOutChanged)

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

    bool isSignedOut() const noexcept;

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

    void signedOutChanged();

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
    // The endpoint's tap, held back until there is a QML scene to receive it.
    //
    // NotificationEndpoint is constructed in this class's constructor, deliberately, so a
    // tap that started the process is not missed - but that constructor runs before
    // setSource, so re-emitting chatRequested straight through fires it into a scene whose
    // Connections do not exist yet. Measured on device: a cold start driven by the real
    // D-Bus call produced no notification-tap marker at all and landed on the chat list,
    // while the identical call against a running app opened the chat in 2.28 s.
    //
    // main.qml's own pendingChatId cannot cover this - it lives inside the handler that
    // never runs.
    void handleChatRequested(const QString &chatId) noexcept;

    void handleResult(td::td_api::Object *object);

    void loadLanguagePack() noexcept;

    // Arms loadLanguagePack for later, when a cached pack has already answered every qsTr
    // and the refresh would only be taking the socket away from the launch. A slot for the
    // same reason as the retry below.
    void refreshLanguagePack() noexcept;

    // Stamps the pack as pulled. A slot because the reply lands on the reader thread and
    // QSettings belongs to this one.
    void recordLanguagePackFetched() noexcept;

    // Re-arms loadLanguagePack after a failed attempt. A slot because the failure is
    // noticed on the reader thread and a timer has to be started on this one.
    void scheduleLanguagePackRetry() noexcept;

    // Everything else TDLib announces exactly once, replayed into a UI that was not there
    // to hear it. Only ever called under the daemon transport, which is the only one where
    // "exactly once" can land in a process that has already exited - but declared
    // unconditionally so moc never has to agree with the build about which slots exist.
    // See the note in AppManager.cpp.
    void restoreState() noexcept;

    // Fires once, a few seconds in. Silent unless startup is incomplete, in which case it
    // names the half that is missing and releases the UI from waiting on the language
    // pack - the only half the app can do without.
    void reportInitializationStall() noexcept;

private:
    void setParameters() noexcept;

    // Queries the current authorization state and replays it as an update, because TDLib
    // only announces changes. See Client::injectUpdate.
    void requestAuthorizationState() noexcept;

    // Arms restoreState. A notification tap has to reach the socket before the replay does,
    // and the two are not ordered by anything - see the comment in AppManager.cpp.
    void scheduleStateRestore() noexcept;

    // Whether the cached language pack is old enough to be worth 1.8 MB. See
    // LanguagePackMaxAgeSeconds.
    bool languagePackIsStale() const noexcept;

    void checkInitializationStatus() noexcept;

    void handleAuthorizationState(const td::td_api::AuthorizationState &authorizationState);
    void handleConnectionState(const td::td_api::ConnectionState &connectionState);

    bool m_isAuthorized{false};

    // False until initialize() runs, which main.qml calls from Component.onCompleted -
    // so it is exactly "the scene exists and its Connections are live". The one thing
    // that distinguishes a tap worth emitting from a tap that would be emitted into
    // nothing.
    bool m_qmlReady{false};

    // A tap that arrived before that, held for initialize() to release. Only ever one:
    // the last tap is the one the user meant, and two taps before the scene exists would
    // otherwise push two chat pages.
    QString m_pendingChatId;

    // One way. Set at the stall deadline and never cleared: there is no reconnect path -
    // the socket is opened once, in Client's constructor - so a transport that is dead at
    // eight seconds is dead for the run. If TDLib does answer late, appInitialized fires
    // and MainPage leaves this state on `initialized` without consulting it.
    bool m_serviceUnreachable{false};

    // Seeded in the constructor from Settings::wasAuthorized, not default-initialised: the
    // whole point is to be right before TDLib answers.
    bool m_signedOut{true};

    // Whether Locale answered from its disk cache, which is what makes the 1.8 MB pack
    // request skippable for this launch. Set in the constructor, read from a reader-thread
    // callback and never written again, so no synchronisation.
    bool m_localeFromCache{false};

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
