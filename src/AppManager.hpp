#pragma once

#include "LanguagePackInfoModel.hpp"
#include "SessionModel.hpp"
// Both included rather than forward declared: unique_ptr needs the complete type wherever
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

class Account;
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

    // Which of the ways it failed, in one line, for the screen that reports it. Empty until
    // there is something to say. Changes with serviceUnreachable and is notified by it -
    // the two are set together and read together.
    Q_PROPERTY(QString serviceError READ serviceError NOTIFY serviceUnreachableChanged)

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
    Q_PROPERTY(Account *account READ account CONSTANT)

    // Every device this account is signed in on. Empty until the page that shows it calls
    // load(): there is no update behind it, so keeping it current for a page nobody has
    // opened would be a request per launch for nothing.
    Q_PROPERTY(SessionModel *sessionModel READ sessionModel CONSTANT)
    Q_PROPERTY(Locale *locale READ locale CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(StorageManager *storageManager READ storageManager CONSTANT)

    Q_PROPERTY(ChatManager *chatManager READ chatManager NOTIFY chatManagerChanged)

    Q_PROPERTY(LanguagePackInfoModel *languagePackInfoModel READ languagePackInfoModel NOTIFY languagePackInfoModelChanged)

    // What TDLib's cache is costing, preformatted ("14.2 MB"), plus this app's own avatar
    // cache - which TDLib knows nothing about and which a "clear cache" that left it behind
    // would be lying about. Empty until requestStorageStatistics has answered.
    Q_PROPERTY(QString cacheSize READ cacheSize NOTIFY cacheSizeChanged)

    // Whether each notification scope is muted. Three of them because Telegram keeps three,
    // and a phone wants groups silent far more often than it wants everything silent.
    // False until loadNotificationSettings has answered, which is also what they read as
    // before there is an account.
    Q_PROPERTY(bool privateChatsMuted READ privateChatsMuted NOTIFY notificationSettingsChanged)
    Q_PROPERTY(bool groupChatsMuted READ groupChatsMuted NOTIFY notificationSettingsChanged)
    Q_PROPERTY(bool channelChatsMuted READ channelChatsMuted NOTIFY notificationSettingsChanged)
public:
    explicit AppManager(QObject *parent = nullptr);

    bool isAuthorized() const noexcept;

    bool isServiceUnreachable() const noexcept;

    const QString &serviceError() const noexcept;

    bool isSignedOut() const noexcept;

    const QString &connectionStateString() const noexcept;

    Client *client() const noexcept;
    Authorization *authorization() const noexcept;

    // The signed-in user's own profile, for the account settings page. Built with the rest
    // of the graph rather than on authorization: it reads through StorageManager, which
    // answers with empty strings until there is an account, and a page that binds to it
    // before then shows empty fields rather than failing to resolve.
    Account *account() const noexcept;
    SessionModel *sessionModel() const noexcept;
    Locale *locale() const noexcept;
    Settings *settings() const noexcept;
    StorageManager *storageManager() const noexcept;

    ChatManager *chatManager() const noexcept;

    LanguagePackInfoModel *languagePackInfoModel() const noexcept;

    const QString &cacheSize() const noexcept;

    bool privateChatsMuted() const noexcept;
    bool groupChatsMuted() const noexcept;
    bool channelChatsMuted() const noexcept;

signals:
    void chatManagerChanged();
    void languagePackInfoModelChanged();

    void authorizedChanged();

    void serviceUnreachableChanged();

    void signedOutChanged();

    void connectionStateChanged();

    void cacheSizeChanged();
    void notificationSettingsChanged();

    void appInitialized();

    // Forwarded from NotificationManager: a system notification was tapped.
    // Relayed straight from NotificationManager; a decimal string, see the note there.
    void chatRequested(const QString &chatId);

public slots:
    // Signs the account out and leaves nothing of it on disk. TDLib's own logOut destroys
    // its database and then closes the instance; what it does not know about is this app's
    // avatar cache, so that goes first - from here rather than from QML, because a page
    // should not have to know what this process writes where.
    //
    // Under the daemon transport this closes *the daemon's* TDLib, which is the one thing
    // AppManager::close is careful never to do. Here it is the point: meegramd exits when
    // TDLib reports authorizationStateClosed, and the UI's reconnect re-activates it with
    // a fresh instance, which is also what clears the titles and names the notifier is
    // holding. See src/daemon/main.cpp.
    //
    // Deliberately keeps the device's own preferences - theme, bubble layout, language -
    // and the language-pack cache. They are not the account's, and a fresh sign-in on the
    // same phone should not arrive in a different theme and with 1.8 MB to re-download.
    void logOut() noexcept;

    // Asks TDLib what its cache weighs. getStorageStatisticsFast rather than
    // getStorageStatistics: the slow one walks every file to attribute bytes per chat, which
    // is a page this app does not have, and on eMMC it is seconds of it.
    void requestStorageStatistics() noexcept;

    // Throws the cache away: TDLib's downloaded files, and this app's masked-avatar cache
    // with them. Keeps the databases - they are the chat list and the message history, not
    // cache - so this frees space without costing a resync.
    void clearCache() noexcept;

    // The mute switch for one scope. `scope` is 0 private, 1 groups, 2 channels, matching
    // the order the settings page lists them in; anything else is ignored.
    //
    // mute_for is a duration rather than a flag - 0 is unmuted, and Telegram's own clients
    // use 2^31-1 for "forever", which is what a switch means by muted. Every other field of
    // scopeNotificationSettings is sent back as it came, because this is a mute switch and
    // not a notification-settings editor: writing defaults into show_preview or the sound id
    // would quietly reset choices made in another client.
    void setScopeMuted(int scope, bool muted) noexcept;

    void loadNotificationSettings() noexcept;

    void close() noexcept;
    void setOption(const QString &name, const QVariant &value);
    void downloadFile(int fileId, int priority, qlonglong offset, qlonglong limit, bool synchronous);

    // Stops a download that is already running, for the tap that started one by mistake -
    // a document on a metered radio has no size ceiling, and the only thing worse than
    // waiting for one is waiting for one nobody asked for.
    //
    // only_if_pending is false: true would cancel only a download that has not begun
    // transferring yet, which is precisely the case the user is never looking at. What is
    // already on disk stays there, and TDLib reports the file as downloadable again.
    void cancelDownloadFile(int fileId);

    void initialize() noexcept;

    // The "Try again" button on MainPage's unreachable screen, and the one thing that gets
    // a dead transport moving again. Reopens the connection and runs startup from the top;
    // false, and nothing changed, if there is still no daemon to reach - so it can be
    // pressed, or scheduled, as many times as it takes.
    bool retry() noexcept;

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

    // Queued from the getStorageStatisticsFast callback, which runs on the TDLib worker
    // thread. Takes the byte count rather than the formatted string so the avatar cache can
    // be added to it here, on the thread that owns the property.
    void setCacheBytes(qlonglong bytes) noexcept;

    // Empties the session's models and the entity store, one turn after signedOut went
    // true. The delay is the point: QML reacts to signedOut by popping the stack and
    // swapping MainPage's Loader, and a chat delegate holds raw Chat* and File* that the
    // store owns - so the last reference may only go once those pages are gone.
    //
    // A zero timer rather than a direct call: Qt drains posted events, including the
    // deleteLater the pop schedules, before it runs timers.
    void clearSession() noexcept;

    // The same, for one scope's answer. See setScopeMuted for what `scope` means.
    void setScopeMuteState(int scope, bool muted) noexcept;

    void handleResult(td::td_api::Object *object);

    // meegramd went away under a running app. Arms the reconnect below rather than
    // reconnecting here: the daemon is usually on its way back - D-Bus reactivates it, an
    // upgrade restarts it - and an attempt made the instant it died is the one attempt
    // guaranteed to find nothing.
    void handleDaemonGone() noexcept;

    // One reconnect attempt, re-arming itself until the budget runs out. A slot so it can
    // be a timer, and separate from retry() so the button does not inherit a background
    // loop the user did not ask for.
    void reconnectToDaemon() noexcept;

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

    // Set at the stall deadline, cleared by a reconnect that works. If TDLib does answer
    // late, appInitialized fires and MainPage leaves this state on `initialized` without
    // consulting it.
    bool m_serviceUnreachable{false};

    // What to put under "Can't reach TDLib". See reportInitializationStall.
    QString m_serviceError;

    // Automatic reconnect attempts left for the disconnect being recovered from. Counted
    // rather than retried forever: a daemon that is coming back is back within a couple of
    // seconds, and one that is not would otherwise have this process blocking its own UI
    // thread inside connect() every few seconds for the rest of the run.
    int m_reconnectsLeft{0};

    // When the current run of those attempts started, so a disconnect that arrives while
    // one is still in progress does not hand out a fresh budget. See handleDaemonGone.
    qint64 m_lastReconnectEpisode{0};

    // Seeded in the constructor from Settings::wasAuthorized, not default-initialised: the
    // whole point is to be right before TDLib answers.
    bool m_signedOut{true};

    // Whether Locale answered from its disk cache, which is what makes the 1.8 MB pack
    // request skippable for this launch. Set in the constructor, read from a reader-thread
    // callback and never written again, so no synchronisation.
    bool m_localeFromCache{false};

    QString m_connectionStateString;

    QString m_cacheSize;

    // Indexed by the same 0/1/2 the QML side uses. std::array rather than three members
    // because every path through them is the same three lines with one index changed.
    std::array<bool, 3> m_scopeMuted{{false, false, false}};

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Authorization> m_authorization;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<Settings> m_settings;

    std::shared_ptr<ChatManager> m_chatManager;
    std::shared_ptr<StorageManager> m_storageManager;

    // After m_storageManager, and it has to stay there: members are constructed in
    // declaration order, and this one is handed that pointer.
    std::shared_ptr<Account> m_account;
    std::unique_ptr<SessionModel> m_sessionModel;

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
