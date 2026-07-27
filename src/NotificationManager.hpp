#pragma once

#include <QDBusContext>
#include <QHash>
#include <QObject>

#include <memory>

class Locale;
class StorageManager;

// Posts Harmattan system notifications for incoming messages.
//
// Talks to the platform notification manager over D-Bus instead of linking
// libmeegotouch for MNotification: QtDBus is already a dependency here, and
// MNotification is a thin wrapper over these same three calls.
//
// One notification per chat, updated in place as further messages arrive and removed
// when that chat is opened. That is what the platform's grouping expects, and what a
// phone wants over one banner per message.
//
// Only fires while the app is running. Harmattan keeps backgrounded apps alive, so
// that covers "minimised", but not "closed": TDLib locks its database directory, so
// a separate always-on daemon cannot coexist with the app and would mean proxying
// the whole update stream over IPC. Autostarting the app is the cheap answer.
class NotificationManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.meegram.Notification")

public:
    NotificationManager(std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale, QObject *parent = nullptr);

signals:
    // A banner was tapped. main.qml turns this into a page push. The id is a decimal
    // string for the same reason every Q_INVOKABLE here takes one: a qlonglong crossing
    // into QML1 is boxed as a double, and the way back is the conversion that corrupts.
    // Nothing in C++ listens to this, so there is no cost to carrying it as text.
    void chatRequested(const QString &chatId);

public slots:
    // The chat on screen. Its pending notification is dropped, and further ones for
    // it are suppressed until another chat is opened. Pass 0 for "none".
    void setActiveChat(qlonglong chatId) noexcept;

    // Called by the notification manager when its banner is tapped. Which chat is
    // encoded in the object path this is registered at, not in an argument: the
    // action string's argument encoding is undocumented, but a path is a plain
    // string either way.
    Q_SCRIPTABLE void activate();

private slots:
    // Wired to chatUpdated. Only ever takes a banner down - a chat read on another
    // device has to clear this one, and that arrives as updateChatReadInbox.
    void handleChatUpdate(qlonglong chatId) noexcept;

    // Wired to chatLastMessageChanged. The only path that may raise a banner.
    void handleNewMessage(qlonglong chatId) noexcept;

private:
    // Shared body of the two slots above. mayPublish is false for a plain chat update,
    // which is what keeps a chat arriving in the store from looking like a new message.
    void evaluateChat(qlonglong chatId, bool mayPublish) noexcept;

    void publish(qlonglong chatId, const QString &summary, const QString &body, const QString &imagePath) noexcept;
    void withdraw(qlonglong chatId) noexcept;

    // Fetches a chat avatar that has not been downloaded yet, so a later notification
    // for the same chat can show it.
    void requestPhotoDownload(int fileId) noexcept;

    // Registers com.meegram on the session bus, once. Returns false if the name was
    // taken, which turns tap-to-open into a no-op but leaves the banners working.
    bool registerService() noexcept;

    // Resolved once, lazily. Zero means the notification manager was unreachable, in
    // which case publishing stays off for the rest of the run rather than blocking on
    // a dead service for every message.
    uint notificationUserId() noexcept;

    std::shared_ptr<StorageManager> m_storageManager;
    std::shared_ptr<Locale> m_locale;

    qlonglong m_activeChatId{0};

    // Unix time at construction. Everything already in the local database is replayed
    // on the first sync; without this a launch after a busy night would raise one
    // banner per chat.
    uint m_startedAt;

    uint m_userId{0};
    bool m_userIdResolved{false};

    bool m_serviceRegistered{false};
    bool m_serviceWarned{false};

    // chatId -> platform notification id, for updating and removing in place.
    QHash<qlonglong, uint> m_published;

    // chatId -> the message the current notification is for. chatUpdated fires on
    // every chat change - title, photo, read state - not just on new messages.
    QHash<qlonglong, qlonglong> m_notifiedMessageId;

    // chatId -> the highest message id ever notified for that chat. Unlike
    // m_notifiedMessageId this is never cleared, so it keeps working after a withdraw
    // and cannot walk backwards. m_startedAt only filters the replay at launch and goes
    // dead once the process has been up a while - which here is the normal case, the
    // client is left running for days - so it cannot be the only thing standing between
    // a re-delivered old message and a banner.
    QHash<qlonglong, qlonglong> m_highWaterMessageId;
};
