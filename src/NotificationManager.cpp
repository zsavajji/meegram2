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

}  // namespace

NotificationManager::NotificationManager(std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale, QObject *parent)
    : QObject(parent)
    , m_storageManager(std::move(storage))
    , m_locale(std::move(locale))
    , m_startedAt(QDateTime::currentDateTime().toTime_t())
{
    // chatUpdated already covers updateChatLastMessage, so this needs no subscriber of
    // its own on the raw update stream.
    connect(m_storageManager.get(), SIGNAL(chatUpdated(qlonglong)), SLOT(handleChatUpdate(qlonglong)));
}

void NotificationManager::setActiveChat(qlonglong chatId) noexcept
{
    m_activeChatId = chatId;

    if (chatId != 0)
        withdraw(chatId);
}

void NotificationManager::handleChatUpdate(qlonglong chatId) noexcept
{
    if (chatId == m_activeChatId)
        return;

    const auto chat = m_storageManager->chat(chatId);
    if (!chat || chat->isMuted())
        return;

    auto *message = chat->lastMessage();
    if (!message || message->isOutgoing() || message->isService())
        return;

    // Unread by message id rather than unreadCount: TDLib sends updateChatReadInbox
    // after updateChatLastMessage, so the count is still the pre-arrival one here.
    if (message->id() <= chat->lastReadInboxMessageId())
    {
        // Read on another device. Drop the banner this phone is still showing.
        withdraw(chatId);
        return;
    }

    if (message->date().toTime_t() < m_startedAt)
        return;

    if (m_notifiedMessageId.value(chatId) == message->id())
        return;

    m_notifiedMessageId.insert(chatId, message->id());

    auto body = Utils::getContent(message, m_storageManager, m_locale);

    // Groups and channels need to say who spoke; a private chat already has it in the
    // summary.
    if (chat->type() != Chat::Private && chat->type() != Chat::Secret)
    {
        const auto sender = Utils::getSenderName(message, m_storageManager);
        if (!sender.isEmpty())
            body.prepend(sender + QLatin1String(": "));
    }

    // The avatar, if it has been downloaded. The field is an imageURI, not a path.
    const auto *photo = chat->photo();
    const auto photoPath = photo ? photo->localPath() : QString();
    const auto imageUri = photoPath.isEmpty() ? QString() : QLatin1String("file://") + photoPath;

    publish(chatId, Utils::getChatTitle(chat, m_storageManager), body, imageUri);
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
        const auto reply = callNotificationManager(
            "updateNotification", {userId, it.value(), QString::fromLatin1(EventType), summary, body, action, imagePath, 0u, QString()});

        // updateNotification answers false, not an error, for an id it no longer
        // knows.
        if (reply.type() != QDBusMessage::ErrorMessage && QDBusReply<bool>(reply).value())
            return;

        // The user dismissed it, or the daemon restarted. Fall through and post a new
        // one instead of silently dropping the message.
        m_published.remove(chatId);
    }

    const auto reply = callNotificationManager(
        "addNotification", {userId, 0u, QString::fromLatin1(EventType), summary, body, action, imagePath, 0u, QString()});

    if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning() << "addNotification failed:" << reply.errorName() << reply.errorMessage();
        return;
    }

    m_published.insert(chatId, QDBusReply<uint>(reply).value());
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

    emit chatRequested(chatId);
}

bool NotificationManager::registerService() noexcept
{
    if (m_serviceAttempted)
        return m_serviceRegistered;

    m_serviceAttempted = true;
    m_serviceRegistered = QDBusConnection::sessionBus().registerService(QLatin1String(ServiceName));

    if (!m_serviceRegistered)
        qWarning() << "tap-to-open disabled, could not register" << ServiceName;

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

        message.setArguments({userId, it.value()});

        QDBusConnection::sessionBus().send(message);
    }

    m_published.remove(chatId);
}

uint NotificationManager::notificationUserId() noexcept
{
    if (m_userIdResolved)
        return m_userId;

    m_userIdResolved = true;

    const auto reply = callNotificationManager("notificationUserId", {});

    if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning() << "notifications disabled, notificationUserId failed:" << reply.errorName() << reply.errorMessage();
        return 0;
    }

    m_userId = QDBusReply<uint>(reply).value();

    return m_userId;
}
