#include "ChatManager.hpp"

#include "Client.hpp"
#include "Common.hpp"
#include "Localization.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QDateTime>
#include <QDebug>

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
    }
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

ChatManager::ChatManager(std::shared_ptr<StorageManager> storageManager, std::shared_ptr<Locale> locale)
    : m_client(storageManager->client())
    , m_locale(std::move(locale))
    , m_storage(std::move(storageManager))
    , m_mainModel(std::make_unique<ChatModel>(std::make_unique<ChatList>(ChatList::Main), m_locale, m_storage))
    , m_archivedModel(std::make_unique<ChatModel>(std::make_unique<ChatList>(ChatList::Archive), m_locale, m_storage))
    , m_folderModel(std::make_unique<ChatFolderModel>())
{
    updateFolderModels();

    connect(m_storage.get(), SIGNAL(chatFoldersUpdated()), SLOT(onChatFoldersUpdated()));
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

bool ChatManager::openChat(qlonglong chatId) noexcept
{
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
        const auto ids = m_storage->chatIds();
        const auto groups = std::ranges::count_if(ids, [](auto id) { return id < 0; });

        qWarning() << "openChat: no chat in storage for id" << chatId << "- storage holds" << ids.size() << "chats," << groups
                   << "of them groups; fetching";

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

    m_client->send(td::td_api::make_object<td::td_api::getChat>(chatId), [this, chatId](auto &&response) {
        const auto failed = response->get_id() == td::td_api::error::ID;

        if (failed)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());
            qWarning() << "getChat failed for" << chatId << error->code_ << QString::fromStdString(error->message_);
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

    emit chatAvailable(chatId, ok);
}

void ChatManager::closeChat(qlonglong chatId) noexcept
{
    m_client->send(td::td_api::make_object<td::td_api::closeChat>(chatId));

    m_infoFormatter = nullptr;
    m_messageModel = nullptr;
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
