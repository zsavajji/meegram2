#include "ChatManager.hpp"

#include "Client.hpp"
#include "Common.hpp"
#include "Localization.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QStringList>

#include <algorithm>
#include <ranges>

ChatInfoFormatter::ChatInfoFormatter(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_chat(std::move(chat))
    , m_locale(std::move(locale))
    , m_storageManager(std::move(storage))
{
    m_actionTimer.setInterval(6000);
    m_actionTimer.setSingleShot(true);

    connect(&m_actionTimer, SIGNAL(timeout()), SLOT(clearChatAction()));
    connect(m_storageManager.get(), SIGNAL(chatActionUpdated(qlonglong, qlonglong, int)), SLOT(handleChatAction(qlonglong, qlonglong, int)));

    initializeMembers();
    updateStatus();
}

void ChatInfoFormatter::handleChatAction(qlonglong chatId, qlonglong senderId, int actionType) noexcept
{
    if (!m_chat || m_chat->id() != chatId)
        return;

    // Our own typing echoes back; showing it in our own header would be odd.
    if (senderId != 0 && senderId == m_storageManager->myId())
        return;

    if (actionType == 0)
    {
        clearChatAction();
        return;
    }

    QString what;

    switch (actionType)
    {
        case td::td_api::chatActionRecordingVoiceNote::ID:
        case td::td_api::chatActionRecordingVideo::ID:
        case td::td_api::chatActionRecordingVideoNote::ID:
            what = tr("RecordingAudio");
            break;
        case td::td_api::chatActionUploadingPhoto::ID:
            what = tr("SendingPhoto");
            break;
        case td::td_api::chatActionUploadingVideo::ID:
        case td::td_api::chatActionUploadingVideoNote::ID:
            what = tr("SendingVideoStatus");
            break;
        case td::td_api::chatActionUploadingDocument::ID:
        case td::td_api::chatActionUploadingVoiceNote::ID:
            what = tr("SendingFile");
            break;
        default:
            what = tr("Typing");
            break;
    }

    // In a group it matters who; in a one to one chat the header already says.
    if (m_chat->type() != Chat::Private && m_chat->type() != Chat::Secret && senderId != 0)
    {
        if (const auto user = m_storageManager->user(senderId))
        {
            what = Utils::getUserShortName(user) + QLatin1String(": ") + what;
        }
    }

    // Restarted on every repeat, so a continuing action keeps the label alive.
    m_actionTimer.start();

    if (m_action != what)
    {
        m_action = what;
        emit statusChanged();
    }
}

void ChatInfoFormatter::clearChatAction() noexcept
{
    m_actionTimer.stop();

    if (!m_action.isEmpty())
    {
        m_action.clear();
        emit statusChanged();
    }
}

QString ChatInfoFormatter::title() const noexcept
{
    return Utils::getChatTitle(m_chat, m_storageManager);
}

QString ChatInfoFormatter::status() const noexcept
{
    // A live action wins over "last seen recently" or the member count, and falls back
    // to it the moment the action expires.
    return m_action.isEmpty() ? m_status : m_action;
}

QString ChatInfoFormatter::username() const noexcept
{
    return Utils::getChatUsername(m_chat, m_storageManager);
}

QString ChatInfoFormatter::bio() const noexcept
{
    return m_user ? m_storageManager->userBio(m_user->id()) : QString();
}

QString ChatInfoFormatter::phoneNumber() const noexcept
{
    const auto number = m_user ? m_user->phoneNumber() : QString();

    // ponytail: "+" and the digits as TDLib gives them, ungrouped. Grouping is per
    // country and worth a table only if the raw form actually reads badly on device.
    return number.isEmpty() ? number : QLatin1Char('+') + number;
}

void ChatInfoFormatter::loadProfile() noexcept
{
    if (!m_user)
        return;

    // StorageManager owns the request and the answer. Sending it from here would mean a
    // callback on the TDLib worker thread holding a formatter that is destroyed the
    // moment another chat is opened.
    m_storageManager->loadUserFullInfo(m_user->id());
}

void ChatInfoFormatter::handleBasicGroupUpdate(qlonglong groupId) noexcept
{
    if (!m_chat || m_chat->typeId() != groupId)
        return;

    if (auto group = m_storageManager->basicGroup(groupId))
    {
        m_basicGroup = std::move(group);
        updateStatus();
    }
}

void ChatInfoFormatter::handleSupergroupUpdate(qlonglong groupId) noexcept
{
    if (!m_chat || m_chat->typeId() != groupId)
        return;

    if (auto group = m_storageManager->supergroup(groupId))
    {
        m_supergroup = std::move(group);
        updateStatus();
    }
}

void ChatInfoFormatter::handleUserUpdate(qlonglong userId) noexcept
{
    if (!m_chat || m_chat->typeId() != userId)
        return;

    if (auto user = m_storageManager->user(userId))
    {
        m_user = std::move(user);
        updateStatus();

        // updateUser carries the phone number and the usernames as well as the status.
        emit profileChanged();
    }
}

void ChatInfoFormatter::handleUserFullInfo(qlonglong userId) noexcept
{
    if (m_user && m_user->id() == userId)
        emit profileChanged();
}

void ChatInfoFormatter::handleChatOnlineMemberCount(qlonglong chatId, int onlineMemberCount) noexcept
{
    if (!m_chat || m_chat->id() != chatId)
        return;

    if (m_onlineMemberCount != onlineMemberCount)
    {
        m_onlineMemberCount = onlineMemberCount;
        updateStatus();
    }
}

void ChatInfoFormatter::initializeMembers() noexcept
{
    const auto chatType = m_chat->type();
    const auto chatTypeId = m_chat->typeId();

    if (chatType == Chat::Private || chatType == Chat::Secret)
    {
        m_user = m_storageManager->user(chatTypeId);

        connect(m_storageManager.get(), SIGNAL(userUpdated(qlonglong)), SLOT(handleUserUpdate(qlonglong)));
        connect(m_storageManager.get(), SIGNAL(userFullInfoUpdated(qlonglong)), SLOT(handleUserFullInfo(qlonglong)));
        connect(m_storageManager.get(), SIGNAL(chatOnlineMemberCountUpdated(qlonglong, int)), SLOT(handleChatOnlineMemberCount(qlonglong, int)));
    }

    if (chatType == Chat::BasicGroup)
    {
        m_basicGroup = m_storageManager->basicGroup(chatTypeId);

        connect(m_storageManager.get(), SIGNAL(basicGroupUpdated(qlonglong)), SLOT(handleBasicGroupUpdate(qlonglong)));
    }

    if (chatType == Chat::Supergroup || chatType == Chat::Channel)
    {
        m_supergroup = m_storageManager->supergroup(chatTypeId);

        connect(m_storageManager.get(), SIGNAL(supergroupUpdated(qlonglong)), SLOT(handleSupergroupUpdate(qlonglong)));
    }
}

void ChatInfoFormatter::updateStatus() noexcept
{
    QString newStatus;

    if (m_basicGroup)
    {
        if (m_basicGroup->status() == BasicGroup::Status::Banned)
            newStatus = tr("YouWereKicked");
        else
            newStatus = formatStatus(m_basicGroup->memberCount(), "Members", "OnlineCount");
    }
    else if (m_supergroup)
    {
        if (!m_supergroup->isChannel() && m_supergroup->status() == Supergroup::Status::Banned)
        {
            newStatus = tr("YouWereKicked");
        }
        else
        {
            int count = getMemberCountWithFallback();
            newStatus = (count <= 0) ? (m_supergroup->hasLocation() ? tr("MegaLocation")
                                                                    : (m_supergroup->activeUsernames().isEmpty() ? tr("MegaPrivate") : tr("MegaPublic")))
                                     : formatStatus(count, "Members", "OnlineCount");
        }
    }
    else if (m_user)
    {
        if (isServiceNotification())
        {
            newStatus = tr("ServiceNotifications");
        }
        else if (m_user->isSupport())
        {
            newStatus = tr("SupportStatus");
        }
        else if (m_user->type() == User::Type::Bot)
        {
            newStatus = tr("Bot");
        }
        else
        {
            newStatus = formatUserStatus();
        }
    }

    if (m_status != newStatus)
    {
        m_status = newStatus;
        emit statusChanged();
    }
}

int ChatInfoFormatter::getMemberCountWithFallback() const noexcept
{
    int count = m_supergroup->memberCount();
    if (count == 0)
    {
        if (const auto fullInfo = m_storageManager->supergroupFullInfo(m_supergroup->id()))
        {
            count = fullInfo->memberCount();
        }
    }

    return count;
}

QString ChatInfoFormatter::formatStatus(int memberCount, const char *memberKey, const char *onlineKey) const noexcept
{
    const auto memberString = m_locale->formatPluralString(memberKey, memberCount);
    if (memberCount <= 1)
        return memberString;

    if (m_onlineMemberCount > 1)
    {
        return memberString + ", " + m_locale->formatPluralString(onlineKey, m_onlineMemberCount);
    }

    return memberString;
}

bool ChatInfoFormatter::isServiceNotification() const noexcept
{
    return std::ranges::contains(ServiceNotificationsUserIds, m_user->id());
}

QString ChatInfoFormatter::formatUserStatus() const noexcept
{
    switch (m_user->status())
    {
        case User::Status::Empty:
            return tr("ALongTimeAgo");
        case User::Status::LastMonth:
            return tr("WithinAMonth");
        case User::Status::LastWeek:
            return tr("WithinAWeek");
        case User::Status::Offline:
            return formatOfflineStatus();
        case User::Status::Online:
            return tr("Online");
        case User::Status::Recently:
            return tr("Lately");
        default:
            return {};
    }
}

QString ChatInfoFormatter::formatOfflineStatus() const noexcept
{
    const auto wasOnline = m_user->wasOnline();
    if (wasOnline.isNull())
        return tr("Invisible");

    const auto currentDate = QDate::currentDate();
    if (currentDate == wasOnline.date())
    {
        return tr("LastSeenFormatted").arg(tr("TodayAtFormatted")).arg(wasOnline.toString(tr("formatterDay12H")));
    }
    else if (wasOnline.date().daysTo(currentDate) < 2)
    {
        return tr("LastSeenFormatted").arg(tr("YesterdayAtFormatted")).arg(wasOnline.toString(tr("formatterDay12H")));
    }

    return tr("LastSeenDateFormatted").arg(tr("formatDateAtTime").arg(wasOnline.toString(tr("formatterYear"))).arg(wasOnline.toString(tr("formatterDay12H"))));
}

namespace {

// Hands an owned QObject to the event loop instead of destroying it here. QML re-reads
// messageModel and chatInfo only once selectedChatChanged has been delivered, and a
// ChatPage being torn down keeps evaluating its bindings for a moment after closeChat -
// so the previous objects have to outlive the call that replaces them. Destroying them
// inline left the page's ListView holding a freed model. Same reason
// updateFolderModels() releases its old models rather than deleting them.
template <typename T>
void disposeLater(std::unique_ptr<T> &owner) noexcept
{
    if (auto *released = owner.release())
        released->deleteLater();
}

}  // namespace

ChatManager::ChatManager(std::shared_ptr<StorageManager> storageManager, std::shared_ptr<Locale> locale)
    : m_client(storageManager->client())
    , m_locale(std::move(locale))
    , m_storage(std::move(storageManager))
    , m_mainModel(std::make_unique<ChatModel>(std::make_unique<ChatList>(ChatList::Main), m_locale, m_storage))
    , m_archivedModel(std::make_unique<ChatModel>(std::make_unique<ChatList>(ChatList::Archive), m_locale, m_storage))
    , m_folderModel(std::make_unique<ChatFolderModel>())
    , m_searchModel(std::make_unique<SearchModel>(m_storage))
{
    updateFolderModels();

    connect(m_storage.get(), SIGNAL(chatFoldersUpdated()), SLOT(onChatFoldersUpdated()));

    // See eventFilter: openChat has to track the window, not just the page.
    qApp->installEventFilter(this);
}

bool ChatManager::eventFilter(QObject *object, QEvent *event) noexcept
{
    if (!m_selectedChat || (event->type() != QEvent::ApplicationActivate && event->type() != QEvent::ApplicationDeactivate))
        return QObject::eventFilter(object, event);

    // Harmattan keeps a backgrounded app alive and its window "visible" - the task
    // switcher draws a live thumbnail of it - so a chat left open stays open across a
    // minimise. TDLib takes openChat to mean the user is reading the chat: it marks
    // messages read there and suppresses their notifications, and meegramd reads it the
    // same way. Neither is true of a chat sitting in the switcher, which is exactly when
    // a banner is wanted.
    //
    // Only the TDLib side is reopened and reclosed. m_selectedChat, the models and the
    // page stay as they are; the user has not left the chat, they have left the app.
    const auto chatId = m_selectedChat->id();

    if (event->type() == QEvent::ApplicationActivate)
        m_client->send(td::td_api::make_object<td::td_api::openChat>(chatId));
    else
        m_client->send(td::td_api::make_object<td::td_api::closeChat>(chatId));

    return QObject::eventFilter(object, event);
}

QObject *ChatManager::folderModel() const noexcept
{
    return m_folderModel.get();
}

QObject *ChatManager::mainModel() const noexcept
{
    return m_mainModel.get();
}

QObject *ChatManager::archivedModel() const noexcept
{
    return m_archivedModel.get();
}

QObject *ChatManager::searchModel() const noexcept
{
    return m_searchModel.get();
}

QList<QObject *> ChatManager::folderModels() const noexcept
{
    QList<QObject *> models;
    models.reserve(m_folderModels.size());
    for (const auto &model : m_folderModels)
    {
        models.append(model.get());
    }

    return models;
}

Chat *ChatManager::selectedChat() const noexcept
{
    return m_selectedChat.get();
}

QObject *ChatManager::chatInfoFormatter() const noexcept
{
    return m_infoFormatter.get();
}

QObject *ChatManager::messageModel() const noexcept
{
    return m_messageModel.get();
}

bool ChatManager::openChat(const QString &rawChatId) noexcept
{
    const auto chatId = toId(rawChatId);

    auto chat = m_storage->chat(chatId);
    if (!chat)
    {
        // Returning false rather than failing silently. main.qml pushed ChatPage
        // regardless of the outcome, so a miss here left a page whose chat, chatInfo
        // and messageModel were all undefined - every binding on it threw, no
        // MessageModel existed to request history, and the result looked like "the chat
        // never loads its messages" with nothing in the log to say why.
        //
        // StorageManager only ever learns a chat from updateNewChat, so anything opened
        // by id that TDLib has not pushed yet lands here - Saved Messages, which opens
        // myId() directly, and a notification tapped before the chat list has loaded.
        // Fetch it instead of giving up; getChat makes TDLib push updateNewChat.
        fetchChat(chatId);
        return false;
    }

    // A fetch for this chat, if there was one, is done with.
    if (m_fetchingChatId == chatId)
        m_fetchingChatId = 0;

    // Only tell TDLib the chat is open once it is actually going to be shown. Sent
    // first, a failed open left the server believing a chat was open that never was -
    // which also suppresses its notifications.
    m_client->send(td::td_api::make_object<td::td_api::openChat>(chatId));

    m_selectedChat = std::move(chat);

    // Before the new ones are built, and not by letting the assignment destroy them.
    disposeLater(m_messageModel);
    disposeLater(m_infoFormatter);

    m_messageModel = std::make_unique<MessageModel>(m_selectedChat, m_locale, m_storage);
    m_infoFormatter = std::make_unique<ChatInfoFormatter>(m_selectedChat, m_locale, m_storage);

    emit selectedChatChanged();
    emit activeChatChanged(chatId);

    return true;
}

void ChatManager::fetchChat(qlonglong chatId) noexcept
{
    // One attempt per chat. The latch is only cleared by an open that succeeds or by an
    // outright error, so a getChat that returns a chat StorageManager still does not hold
    // fails once and stops, rather than looping openChat -> fetch -> openChat.
    if (m_fetchingChatId == chatId)
        return;

    m_fetchingChatId = chatId;

    // A positive chat id is a user id - TDLib numbers a private chat after the user it is
    // with, and every other kind of chat negative. getChat only knows chats that already
    // exist, and a contact you have never written to has none, so it answers "Chat not
    // found" for exactly the case the contact search exists to serve. createPrivateChat
    // creates it instead, and returns the existing one when there is one. force is false
    // so the real title and photo come back rather than whatever is cached locally.
    auto request = chatId > 0 ? td::td_api::object_ptr<td::td_api::Function>(td::td_api::make_object<td::td_api::createPrivateChat>(chatId, false))
                              : td::td_api::object_ptr<td::td_api::Function>(td::td_api::make_object<td::td_api::getChat>(chatId));

    m_client->send(std::move(request), [this, chatId](auto &&response) {
        const auto failed = response->get_id() == td::td_api::error::ID;

        if (failed)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());
            qWarning() << "fetching chat" << chatId << "failed:" << error->code_ << QString::fromStdString(error->message_);
        }

        // This runs on the TDLib worker thread. Hop to the main thread before touching
        // anything or emitting.
        //
        // TDLib sends updateNewChat before the reply that needs it, and Client emits
        // updates through a queued connection as well, so by the time this queued call
        // is delivered StorageManager has already processed the update.
        QMetaObject::invokeMethod(this, "handleChatFetched", Qt::QueuedConnection, Q_ARG(qlonglong, chatId), Q_ARG(bool, !failed));
    });
}

void ChatManager::handleChatFetched(qlonglong chatId, bool ok) noexcept
{
    if (!ok)
        m_fetchingChatId = 0;  // a network failure should not block a later attempt

    emit chatAvailable(QString::number(chatId), ok);
}

void ChatManager::closeChat(const QString &rawChatId) noexcept
{
    const auto chatId = toId(rawChatId);

    m_client->send(td::td_api::make_object<td::td_api::closeChat>(chatId));

    // Only tear down the selection if this really is the chat that is selected. A page
    // being destroyed can reach here after a *different* chat has been opened: tapping a
    // notification pops the current ChatPage and opens the new chat in the same turn,
    // and QML destroys the popped page afterwards. Without this, that destruction closed
    // the chat that had just been opened, leaving the newly pushed page bound to a model
    // already on its way out - and no selectedChatChanged to tell QML to re-read.
    if (!m_selectedChat || m_selectedChat->id() != chatId)
        return;

    // This also runs while ChatPage is being popped, so its bindings can still fire
    // against both of these. Destroying them here is what a segfault on leaving a chat
    // looks like.
    disposeLater(m_infoFormatter);
    disposeLater(m_messageModel);

    m_selectedChat = nullptr;

    emit activeChatChanged(0);
}

void ChatManager::onChatFoldersUpdated() noexcept
{
    updateFolderModels();
    emit folderModelsChanged();
}

void ChatManager::updateFolderModels() noexcept
{
    const auto &chatFolders = m_storage->chatFolders();

    // This runs from the constructor and again on every updateChatFolders push, and
    // previously only ever appended. Each leftover ChatModel stayed connected to
    // chatUpdated/chatPositionUpdated for the life of the process, so the per-update
    // work grew without bound.
    //
    // Hand the old models to the event loop rather than destroying them here: QML
    // holds the previous folderModels list until it re-reads the property after
    // folderModelsChanged(), so they must outlive the current call.
    for (auto &model : m_folderModels)
    {
        if (auto *released = model.release())
        {
            released->deleteLater();
        }
    }

    m_folderModels.clear();
    m_folderModels.reserve(chatFolders.size());

    std::ranges::for_each(chatFolders, [this](const auto &folder) {
        m_folderModels.emplace_back(std::make_unique<ChatModel>(std::make_unique<ChatList>(ChatList::Folder, folder->id()), m_locale, m_storage));
    });

    m_folderModel->setItems(chatFolders);
}
