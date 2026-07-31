#include "NotificationManager.hpp"

#include "Chat.hpp"
#include "File.hpp"
#include "Localization.hpp"
#include "Message.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDebug>
#include <QWidget>

namespace {

// libmeegotouch's MNotification talks to this; there is no freedesktop.Notifications
// service on Harmattan.
constexpr auto NotificationService = "com.meego.core.MNotificationManager";
constexpr auto NotificationPath = "/notificationmanager";
constexpr auto NotificationInterface = "com.meego.core.MNotificationManager";

// One of the event types under /usr/share/meegotouch/notifications/eventtypes. It
// picks the icon, sound and vibration; this is the platform's instant-message one.
constexpr auto EventType = "x-nokia.messaging.im";

// What the notification manager calls back when a banner is tapped. Matches the
// D-Bus service file installed under /usr/share/dbus-1/services, so a tap starts the
// app if it is no longer running.
constexpr auto ServiceName = "com.meegram";
constexpr auto ActionInterface = "com.meegram.Notification";
constexpr auto ActionMethod = "activate";

// Object path components accept [A-Za-z0-9_] only, and supergroup ids are negative.
QString objectPathFor(qlonglong chatId)
{
    const auto suffix = chatId < 0 ? QLatin1String("n") + QString::number(-chatId) : QString::number(chatId);

    return QLatin1String("/chat/") + suffix;
}

qlonglong chatIdFromPath(const QString &path)
{
    const auto tail = path.section(QLatin1Char('/'), -1);

    return tail.startsWith(QLatin1Char('n')) ? -tail.mid(1).toLongLong() : tail.toLongLong();
}

// The MRemoteAction wire format: service, object path, interface and method,
// space separated. Arguments would follow, but encoding them is the part that is
// undocumented - hence the chat id living in the path instead.
QString remoteAction(qlonglong chatId)
{
    return QString::fromLatin1(ServiceName) + QLatin1Char(' ') + objectPathFor(chatId) + QLatin1Char(' ') + QLatin1String(ActionInterface) +
           QLatin1Char(' ') + QLatin1String(ActionMethod);
}

// Whether the user is actually looking at the app.
//
// isVisible() is no use here: Harmattan keeps a minimised application visible - the task
// switcher renders a live thumbnail of it - and this client is expected to stay running
// precisely so notifications keep arriving. Activation is the thing that tracks whether
// the window has the user's attention.
bool isForeground()
{
    for (auto *widget : QApplication::topLevelWidgets())
    {
        if (widget->isActiveWindow())
            return true;
    }

    return false;
}

// Blocking, because addNotification returns the id needed to update or remove it
// later. Short timeout so a wedged notification daemon cannot freeze the UI for the
// 25 seconds QtDBus would otherwise wait.
constexpr int CallTimeoutMs = 2000;

QDBusMessage callNotificationManager(const char *method, const QVariantList &arguments)
{
    auto message = QDBusMessage::createMethodCall(NotificationService, NotificationPath, NotificationInterface, QLatin1String(method));

    message.setArguments(arguments);

    return QDBusConnection::sessionBus().call(message, QDBus::Block, CallTimeoutMs);
}

// addNotification and updateNotification take the same nine arguments; the second is
// the group id for one and the notification id for the other.
//
// Built with operator<< rather than a braced list: Qt 4.7 predates QList's
// initializer_list constructor, which only arrived in 4.8.
QVariantList notificationArguments(uint userId, uint id, const QString &summary, const QString &body, const QString &action,
                                   const QString &imageUri)
{
    QVariantList arguments;

    arguments << userId << id << QString::fromLatin1(EventType) << summary << body << action << imageUri << 0u << QString();

    return arguments;
}

}  // namespace

NotificationManager::NotificationManager(std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale, QObject *parent)
    : QObject(parent)
    , m_storageManager(std::move(storage))
    , m_locale(std::move(locale))
    , m_startedAt(QDateTime::currentDateTime().toTime_t())
{
    // chatUpdated already covers updateChatLastMessage, so this needs no subscriber of
    // its own on the raw update stream.
    // Two signals, two jobs. chatUpdated is the broad one - it fires when a chat merely
    // arrives in the store, which a scroll of the chat list does in batches - so it may
    // only take banners down. Raising one needs the narrow signal.
    connect(m_storageManager.get(), SIGNAL(chatUpdated(qlonglong)), SLOT(handleChatUpdate(qlonglong)));
    connect(m_storageManager.get(), SIGNAL(chatLastMessageChanged(qlonglong)), SLOT(handleNewMessage(qlonglong)));
}

void NotificationManager::setActiveChat(qlonglong chatId) noexcept
{
    m_activeChatId = chatId;

    if (chatId != 0)
        withdraw(chatId);
}

void NotificationManager::handleChatUpdate(qlonglong chatId) noexcept
{
    evaluateChat(chatId, false);
}

void NotificationManager::handleNewMessage(qlonglong chatId) noexcept
{
    evaluateChat(chatId, true);
}

void NotificationManager::evaluateChat(qlonglong chatId, bool mayPublish) noexcept
{
    const auto chat = m_storageManager->chat(chatId);
    if (!chat)
        return;

    auto *message = chat->lastMessage();
    if (!message || message->isOutgoing() || message->isService())
        return;

    // Unread by message id rather than unreadCount: TDLib sends updateChatReadInbox
    // after updateChatLastMessage, so the count is still the pre-arrival one here.
    // Checked before the dedupe below, or a message read elsewhere after we had already
    // notified would never get its banner taken down.
    if (message->id() <= chat->lastReadInboxMessageId())
    {
        // Read here or on another device. Drop the banner this phone is still showing.
        withdraw(chatId);
        return;
    }

    // Everything past here can only raise a banner, so a plain chat update stops.
    // Without this a scroll of the chat list notifies: updateNewChat arrives with
    // last_message_ already filled in, and a chat being delivered to the store for the
    // first time looks exactly like a message landing in it.
    if (!mayPublish)
        return;

    // Being in the store is not being subscribed. TDLib admits a chat the moment it is
    // referenced anywhere - the origin of a forward, a search hit, the sender of a
    // message in a group someone else is in - and keeps sending updates for it. None of
    // that means the user follows it. Membership of a list lives in positions_, which
    // stays empty for a chat that only exists in the store, and setPositions() has
    // already run for this update by the time we get here.
    if (chat->positions().empty())
        return;

    // Never step backwards. Unlike m_notifiedMessageId this survives a withdraw, so a
    // re-delivered or out-of-order update cannot raise a banner for a message this chat
    // has already notified past.
    if (message->id() <= m_highWaterMessageId.value(chatId, 0))
        return;

    if (m_notifiedMessageId.value(chatId) == message->id())
        return;

    // Open is not enough on its own: this client is left running so notifications keep
    // arriving, so a chat can be "open" for days while the app sits in the switcher.
    // Only stay quiet when the chat is open *and* on screen.
    if (chatId == m_activeChatId && isForeground())
        return;

    if (chat->isMuted())
        return;

    if (message->date().toTime_t() < m_startedAt)
        return;

    m_notifiedMessageId.insert(chatId, message->id());
    m_highWaterMessageId.insert(chatId, message->id());

    auto body = Utils::getContent(message, m_storageManager, m_locale);

    // Groups and channels need to say who spoke; a private chat already has it in the
    // summary.
    if (chat->type() != Chat::Private && chat->type() != Chat::Secret)
    {
        const auto sender = Utils::getSenderName(message, m_storageManager);
        if (!sender.isEmpty())
            body.prepend(sender + QLatin1String(": "));
    }

    // The avatar. A plain absolute path, not a file:// URL - MNotification::setImage()
    // takes "a path to an image file or an icon id", and a URL draws the broken-image
    // red square.
    //
    // This was changed to file:// once on the reading that a bare path was refused and
    // took the whole banner with it. That was wrong: the missing banners were
    // notificationUserId() latching an early failure, which suppressed every
    // notification for the session no matter what was in this field. The two symptoms
    // overlapped and the image got the blame.
    //
    // Still gated on isDownloadingCompleted(): TDLib fills localPath in as soon as a
    // download *starts*, and a half-written file draws the same red square.
    QString imagePath;

    if (auto *photo = chat->photo())
    {
        if (photo->isDownloadingCompleted())
        {
            imagePath = photo->localPath();
        }
        else if (photo->canBeDownloaded() && !photo->isDownloadingActive())
        {
            // Nothing to show this time. The chat list only fetches avatars for rows
            // it has actually drawn, and a notification can arrive for a chat that was
            // never scrolled to - so ask now, and the next one will have it.
            requestPhotoDownload(photo->id());
        }
    }

    publish(chatId, Utils::getChatTitle(chat, m_storageManager), body, imagePath);
}

void NotificationManager::publish(qlonglong chatId, const QString &summary, const QString &body, const QString &imagePath) noexcept
{
    const auto userId = notificationUserId();
    if (userId == 0)
        return;

    // Tap-to-open is best effort: without it the banner still shows, it just does
    // nothing when touched. registerObject returns false for a path that is already
    // taken, which after the first message from this chat it is - so the result is
    // deliberately not what decides whether to hand out an action.
    if (registerService())
        QDBusConnection::sessionBus().registerObject(objectPathFor(chatId), this, QDBusConnection::ExportScriptableSlots);

    const auto action = m_serviceRegistered ? remoteAction(chatId) : QString();

    // Replace the chat's existing banner rather than stacking a second one.
    if (const auto it = m_published.constFind(chatId); it != m_published.constEnd())
    {
        const auto reply =
            callNotificationManager("updateNotification", notificationArguments(userId, it.value(), summary, body, action, imagePath));

        // updateNotification answers false, not an error, for an id it no longer
        // knows.
        if (reply.type() != QDBusMessage::ErrorMessage && QDBusReply<bool>(reply).value())
            return;

        // The user dismissed it, or the daemon restarted. Fall through and post a new
        // one instead of silently dropping the message.
        m_published.remove(chatId);
    }

    // Group 0: this client does not use notification groups.
    auto reply = callNotificationManager("addNotification", notificationArguments(userId, 0u, summary, body, action, imagePath));

    // The avatar is decoration and must never be able to cost you the banner. Whether
    // this field wants a bare path or a URI is not something the platform documents
    // usefully, so if the call is refused while carrying one, drop it and post again.
    //
    // A returned id of zero counts as refused too. An outright D-Bus error was the only
    // thing being retried, which misses a daemon that accepts the call and quietly
    // declines to post it - and "accepted, nothing appeared" is exactly the shape of
    // this bug.
    const auto rejected = [](const QDBusMessage &message) {
        return message.type() == QDBusMessage::ErrorMessage || QDBusReply<uint>(message).value() == 0;
    };

    if (rejected(reply) && !imagePath.isEmpty())
    {
        qWarning() << "notification: addNotification rejected carrying an image -" << reply.errorName() << reply.errorMessage()
                   << "- retrying without" << imagePath;

        reply = callNotificationManager("addNotification", notificationArguments(userId, 0u, summary, body, action, QString()));
    }

    if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning() << "notification: addNotification failed:" << reply.errorName() << reply.errorMessage();
        return;
    }

    const auto published = QDBusReply<uint>(reply).value();

    m_published.insert(chatId, published);
}

void NotificationManager::activate()
{
    const auto chatId = chatIdFromPath(message().path());

    // Tapped from the lock screen or the event feed, so the window is very likely
    // behind something. There is exactly one top level widget here - the viewer.
    for (auto *widget : QApplication::topLevelWidgets())
    {
        widget->raise();
        widget->activateWindow();
    }

    withdraw(chatId);

    emit chatRequested(QString::number(chatId));
}

void NotificationManager::requestPhotoDownload(int fileId) noexcept
{
    if (auto client = m_storageManager->client())
    {
        client->send(td::td_api::make_object<td::td_api::downloadFile>(fileId, 1, 0, 0, false));
    }
}

bool NotificationManager::registerService() noexcept
{
    if (m_serviceRegistered)
        return true;

    m_serviceRegistered = QDBusConnection::sessionBus().registerService(QLatin1String(ServiceName));

    // Latched only on success, for the same reason as notificationUserId(): the session
    // bus may not have the name available when the first message arrives, and a single
    // early failure used to disable tap-to-open for the rest of the session. Retried on
    // every publish now, but the warning stays a one-off - this runs per notification.
    if (!m_serviceRegistered)
    {
        if (!m_serviceWarned)
            qWarning() << "tap-to-open disabled, could not register" << ServiceName;

        m_serviceWarned = true;
    }

    return m_serviceRegistered;
}

void NotificationManager::withdraw(qlonglong chatId) noexcept
{
    m_notifiedMessageId.remove(chatId);

    QDBusConnection::sessionBus().unregisterObject(objectPathFor(chatId));

    const auto it = m_published.constFind(chatId);
    if (it == m_published.constEnd())
        return;

    const auto userId = notificationUserId();
    if (userId != 0)
    {
        // No reply needed, so this one does not block.
        auto message =
            QDBusMessage::createMethodCall(NotificationService, NotificationPath, NotificationInterface, QLatin1String("removeNotification"));

        QVariantList arguments;
        arguments << userId << it.value();

        message.setArguments(arguments);

        QDBusConnection::sessionBus().send(message);
    }

    m_published.remove(chatId);
}

uint NotificationManager::notificationUserId() noexcept
{
    if (m_userIdResolved)
        return m_userId;

    const auto reply = callNotificationManager("notificationUserId", {});

    if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning() << "notification: notificationUserId failed:" << reply.errorName() << reply.errorMessage();
        return 0;
    }

    const auto userId = QDBusReply<uint>(reply).value();

    if (userId == 0)
    {
        qWarning() << "notification: notificationUserId returned 0";
        return 0;
    }

    // Latched only on success. This used to set m_userIdResolved before making the call,
    // so a single early failure - the notification daemon not being up yet when the first
    // message arrives, which at startup it may well not be - disabled notifications for
    // the rest of the session with nothing said after the first line.
    m_userIdResolved = true;
    m_userId = userId;

    return m_userId;
}
