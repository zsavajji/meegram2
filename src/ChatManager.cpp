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

bool ChatInfoFormatter::canSendMessages() const noexcept
{
    if (!m_chat || m_chat->type() != Chat::Channel)
        return true;

    // Read-only until the supergroup lands, not the other way round: a composer that
    // appears and then vanishes a moment later is worse than one that appears late.
    if (!m_supergroup)
        return false;

    // ponytail: any admin counts. chatMemberStatusAdministrator carries a can_post_messages
    // right that Supergroup collapses into this enum, so an admin restricted to editing
    // others' posts still gets the composer - and TDLib refuses the send. Widen Supergroup
    // to keep the rights if that turns up.
    const auto status = m_supergroup->status();
    return status == Supergroup::Status::Creator || status == Supergroup::Status::Administrator;
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

        // Unconditional: the status string is unchanged when only the member's own rights
        // move, which is exactly the update the composer cares about.
        emit canSendMessagesChanged();
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

Chat *ChatManager::profileChat() const noexcept
{
    return m_profileChat.get();
}

QObject *ChatManager::profileInfoFormatter() const noexcept
{
    return m_profileInfo.get();
}

QString ChatManager::profileChatId() const noexcept
{
    return m_profileChat ? QString::number(m_profileChat->id()) : QString();
}

QString ChatManager::selectedChatId() const noexcept
{
    return m_selectedChat ? QString::number(m_selectedChat->id()) : QString();
}

void ChatManager::setProfileChat(std::shared_ptr<Chat> chat) noexcept
{
    m_profileChat = std::move(chat);

    // Before the new one is built, and not by letting the assignment destroy it - same
    // reason openChat() disposes of its formatter that way.
    disposeLater(m_profileInfo);

    m_profileInfo = std::make_unique<ChatInfoFormatter>(m_profileChat, m_locale, m_storage);

    emit profileChanged();
}

void ChatManager::openProfile(const QString &target) noexcept
{
    // A mention carries either a user id, which is also the id of the private chat with
    // them, or an @username that has to be resolved. toId gives 0 for the latter.
    const auto chatId = toId(target);

    if (chatId != 0)
    {
        if (auto chat = m_storage->chat(chatId))
        {
            setProfileChat(std::move(chat));
            emit profileReady(true, QString());
            return;
        }
    }

    // Not known here yet. searchPublicChat resolves a username; the id path is the same
    // createPrivateChat / getChat split fetchChat makes, and for the same reason - a
    // user you have never written to has no chat until one is created.
    auto request = chatId == 0
                       ? td::td_api::object_ptr<td::td_api::Function>(td::td_api::make_object<td::td_api::searchPublicChat>(
                             QString(target).remove(QLatin1Char('@')).toStdString()))
                       : chatId > 0 ? td::td_api::object_ptr<td::td_api::Function>(
                                          td::td_api::make_object<td::td_api::createPrivateChat>(chatId, false))
                                    : td::td_api::object_ptr<td::td_api::Function>(td::td_api::make_object<td::td_api::getChat>(chatId));

    m_client->send(std::move(request), [this, target](auto &&response) {
        qlonglong resolvedId = 0;
        QString reason;

        if (response->get_id() == td::td_api::chat::ID)
        {
            auto chat = td::td_api::move_object_as<td::td_api::chat>(response);

            resolvedId = chat->id_;

            // The reply is the chat, so hand it to everyone the same way fetchChat does -
            // updateNewChat is what StorageManager learns a chat from, and TDLib sends it
            // once per process however many times this client is restarted.
            m_client->injectUpdate(td::td_api::make_object<td::td_api::updateNewChat>(std::move(chat)));
        }
        else if (response->get_id() == td::td_api::error::ID)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());

            reason = QString::fromStdString(error->message_) + QLatin1String(" (") + QString::number(error->code_) + QLatin1Char(')');
            qWarning() << "opening profile" << target << "failed:" << reason;
        }
        else
        {
            // Neither a chat nor an error: the reply decoded as something this does not
            // know what to do with, which is worth seeing rather than swallowing.
            reason = QLatin1String("unexpected reply ") + QString::number(response->get_id());
            qWarning() << "opening profile" << target << reason;
        }

        // Queued behind the injected update, which Qt delivers in the order posted, so
        // StorageManager holds the chat by the time this runs.
        QMetaObject::invokeMethod(this, "handleProfileFetched", Qt::QueuedConnection, Q_ARG(qlonglong, resolvedId),
                                  Q_ARG(QString, reason));
    });
}

void ChatManager::handleProfileFetched(qlonglong chatId, const QString &reason) noexcept
{
    if (chatId != 0)
    {
        if (auto chat = m_storage->chat(chatId))
        {
            setProfileChat(std::move(chat));
            emit profileReady(true, QString());
            return;
        }
    }

    // The two failures are worth telling apart: the request itself came back with
    // something, and separately StorageManager did or did not end up holding the chat it
    // was told about. The second one means the injected update was not taken, which is
    // the daemon-shaped bug fetchChat exists to work around.
    emit profileReady(false, !reason.isEmpty() ? reason : QString::fromLatin1("chat %1 resolved but not in store").arg(chatId));
}

void ChatManager::searchMentions(const QString &query) noexcept
{
    const auto type = m_selectedChat ? m_selectedChat->type() : Chat::Type::None;

    // Only a group has members worth suggesting. A private chat has exactly one other
    // person and you are not going to mention them by name.
    if (type != Chat::Type::BasicGroup && type != Chat::Type::Supergroup)
    {
        emit mentionsFound({}, {}, {});
        return;
    }

    // Ten is about three screens of the panel this fills, and the query narrows fast.
    // Null filter: every member, in the order the server ranks them.
    m_client->send(td::td_api::make_object<td::td_api::searchChatMembers>(m_selectedChat->id(), query.toStdString(), 10, nullptr),
                   [this](auto &&response) {
                       // Straight to the main thread: turning member ids into usernames
                       // reads StorageManager, which belongs to that thread alone.
                       QMetaObject::invokeMethod(this, "handleChatMembers", Qt::QueuedConnection, Q_ARG(void *, response.release()));
                   });
}

void ChatManager::handleChatMembers(void *responseObject) noexcept
{
    const td::td_api::object_ptr<td::td_api::Object> response(static_cast<td::td_api::Object *>(responseObject));

    // Paired by index. The username is what gets inserted when there is one; the name is
    // what makes the row recognisable, and is itself what gets inserted for a member with
    // no username, with the id carried alongside so the message can point at them.
    QStringList usernames, names, userIds;

    if (response && response->get_id() == td::td_api::chatMembers::ID)
    {
        const auto *members = static_cast<const td::td_api::chatMembers *>(response.get());

        for (const auto &member : members->members_)
        {
            if (!member->member_id_ || member->member_id_->get_id() != td::td_api::messageSenderUser::ID)
                continue;

            const auto userId = static_cast<const td::td_api::messageSenderUser *>(member->member_id_.get())->user_id_;

            const auto user = m_storage->user(userId);

            if (!user)
                continue;

            // First and last together, not getUserName - that one gives the short name,
            // which is the first name alone and tells two Andreas apart from nothing.
            auto name = (user->firstName() + QLatin1Char(' ') + user->lastName()).trimmed();

            if (name.isEmpty())
                name = Utils::getUserShortName(user);

            const auto username = user->activeUsernames().isEmpty() ? QString() : user->activeUsernames().first();

            // Nothing to show and nothing to insert.
            if (username.isEmpty() && name.isEmpty())
                continue;

            usernames.append(username);
            names.append(name);
            userIds.append(QString::number(userId));
        }
    }

    emit mentionsFound(usernames, names, userIds);
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
        else if (response->get_id() == td::td_api::chat::ID)
        {
            // The reply *is* the chat, so use it. This used to rely on TDLib pushing
            // updateNewChat ahead of the reply, which it does exactly once per TDLib
            // process and never again - and meegramd's TDLib outlives the UI by design.
            // So against a daemon that had already announced this chat to some earlier
            // run, the reply arrived, StorageManager still did not have the chat, the
            // retry missed again, and fetchChat's one-attempt latch stopped it there: a
            // tapped notification opened nothing at all.
            //
            // It only ever appeared to work because getCurrentState's replay carried
            // updateNewChat for every chat, so the open succeeded when the whole 5.5 MB
            // of it had been decoded. That is what made tapping a banner take twenty
            // seconds rather than the ten milliseconds this reply actually costs.
            //
            // injectUpdate rather than a private path into StorageManager: an update is
            // what every subscriber already knows how to take, and it is the same
            // mechanism restoreState and requestAuthorizationState use for the rest of
            // what TDLib says exactly once.
            m_client->injectUpdate(
                td::td_api::make_object<td::td_api::updateNewChat>(td::td_api::move_object_as<td::td_api::chat>(response)));
        }

        // This runs on the TDLib worker thread. Hop to the main thread before touching
        // anything or emitting.
        //
        // Queued after the injected update, and Qt delivers queued calls to a thread in
        // the order they were posted - so StorageManager has taken the chat by the time
        // this runs, which is the ordering handleChatFetched's caller depends on.
        QMetaObject::invokeMethod(this, "handleChatFetched", Qt::QueuedConnection, Q_ARG(qlonglong, chatId), Q_ARG(bool, !failed));
    });
}

void ChatManager::createGroup(const QString &title, const QStringList &userIds) noexcept
{
    std::vector<std::int64_t> memberIds;
    memberIds.reserve(userIds.size());

    for (const auto &rawUserId : userIds)
    {
        memberIds.push_back(toId(rawUserId));
    }

    // TDLib refuses an empty member list, and a group of one is not a group. The page keeps
    // its button disabled until somebody is picked; this is the backstop.
    if (memberIds.empty())
    {
        emit chatAvailable(QString::number(0), false);
        return;
    }

    // 0: no auto-delete timer, which is what the official clients create a group with.
    m_client->send(td::td_api::make_object<td::td_api::createNewBasicGroupChat>(std::move(memberIds), title.trimmed().toStdString(), 0),
                   [this](auto &&response) {
                       qlonglong chatId = 0;

                       if (response->get_id() == td::td_api::createdBasicGroupChat::ID)
                       {
                           const auto *created = static_cast<const td::td_api::createdBasicGroupChat *>(response.get());
                           chatId = created->chat_id_;

                           // The group exists either way - somebody whose privacy settings
                           // refuse invites is left out of it, not a reason to fail.
                           if (created->failed_to_add_members_ && !created->failed_to_add_members_->failed_to_add_members_.empty())
                           {
                               qWarning() << "createNewBasicGroupChat could not add"
                                          << static_cast<int>(created->failed_to_add_members_->failed_to_add_members_.size()) << "of the members";
                           }
                       }
                       else if (response->get_id() == td::td_api::error::ID)
                       {
                           const auto *error = static_cast<const td::td_api::error *>(response.get());
                           qWarning() << "createNewBasicGroupChat failed:" << error->code_ << QString::fromStdString(error->message_);
                       }

                       // Worker thread; hop before emitting. handleChatFetched is the same
                       // "this chat is ready to open" report a fetch by id makes, so main.qml
                       // needs nothing new to push the new group's page.
                       QMetaObject::invokeMethod(this, "handleChatFetched", Qt::QueuedConnection, Q_ARG(qlonglong, chatId), Q_ARG(bool, chatId != 0));
                   });
}

void ChatManager::handleChatFetched(qlonglong chatId, bool ok) noexcept
{
    // Only the chat still being waited on. fetchChat keeps one latch, so opening a second
    // chat before the first reply lands overwrites it - and main.qml turns every
    // chatAvailable into a pageStack.push, so without this the late reply for the
    // abandoned chat pushes a page the user has already navigated away from.
    if (m_fetchingChatId != chatId)
        return;

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
