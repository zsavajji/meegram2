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

namespace {

// One page of members, which is the whole list for a basic group and the recent slice of
// a supergroup. Enough to fill a profile page several times over on a 480px screen.
constexpr int MaxMembers = 200;

// What the member list is ordered by. Online sorts above every timestamp; the three
// hidden statuses carry none of their own, so each takes the top of the window it stands
// for - which is what puts somebody "seen recently" above somebody genuinely last seen a
// month ago, and keeps the three of them in order among themselves.
qint64 lastSeen(const std::shared_ptr<User> &user) noexcept
{
    constexpr qint64 Day = 24 * 60 * 60;

    const auto now = static_cast<qint64>(QDateTime::currentDateTime().toTime_t());

    switch (user->status())
    {
        case User::Status::Online:
            return std::numeric_limits<qint64>::max();
        case User::Status::Offline: {
            const auto wasOnline = user->wasOnline();
            return wasOnline.isNull() ? 0 : static_cast<qint64>(wasOnline.toTime_t());
        }
        case User::Status::Recently:
            return now - Day;
        case User::Status::LastWeek:
            return now - 7 * Day;
        case User::Status::LastMonth:
            return now - 30 * Day;
        default:
            return 0;
    }
}

}  // namespace

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

ChatInfoFormatter::~ChatInfoFormatter()
{
    // Tell any in-flight member-list callback not to touch this object.
    m_alive->store(false);
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

QVariantList ChatInfoFormatter::members() const noexcept
{
    return m_members;
}

bool ChatInfoFormatter::membersLoading() const noexcept
{
    return m_membersLoading;
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

void ChatInfoFormatter::loadMembers() noexcept
{
    if (!m_chat)
        return;

    td::td_api::object_ptr<td::td_api::Function> request;

    switch (m_chat->type())
    {
        case Chat::BasicGroup:
            // A basic group carries its whole membership in its full info - there is no
            // paged request for one, and there is no need: they cap at 200 people.
            request = td::td_api::make_object<td::td_api::getBasicGroupFullInfo>(m_chat->typeId());
            break;
        case Chat::Supergroup:
            // Recent, which is TDLib's "most recently active first" - the same order this
            // list wants, though it is sorted here anyway because the basic-group answer
            // arrives in no particular order.
            //
            // ponytail: the first page only. A supergroup runs to hundreds of thousands of
            // members and this is a phone; the page shows who is around, not a directory.
            // Feed the offset back in behind a "load more" if that ever bites.
            request = td::td_api::make_object<td::td_api::getSupergroupMembers>(
                m_chat->typeId(), td::td_api::make_object<td::td_api::supergroupMembersFilterRecent>(), 0, MaxMembers);
            break;
        default:
            // A private chat has no members, and a channel only lets its admins ask.
            return;
    }

    // Past the switch, so only a chat that really is asking shows a spinner.
    m_membersLoading = true;
    emit membersLoadingChanged();

    m_storageManager->client()->send(std::move(request), [this, alive = m_alive](auto &&response) {
        // Worker thread, and this formatter may already be gone: opening another profile
        // replaces it while the request is still out. Nothing else is touched here - the
        // members are resolved through StorageManager on the other side of the hop.
        if (!alive->load())
            return;

        QMetaObject::invokeMethod(this, "handleChatMembers", Qt::QueuedConnection, Q_ARG(void *, response.release()));
    });
}

void ChatInfoFormatter::handleChatMembers(void *responseObject) noexcept
{
    td::td_api::object_ptr<td::td_api::Object> response(static_cast<td::td_api::Object *>(responseObject));

    // The answer is here, whatever it turned out to be. Cleared before the branches below
    // rather than on the way out of each, so no early return - and two of them are error
    // paths - can leave the spinner running for the life of the page.
    if (m_membersLoading)
    {
        m_membersLoading = false;
        emit membersLoadingChanged();
    }

    if (!response)
        return;

    // The two requests answer with different objects carrying the same array. Nothing
    // else about them is used, so they are unwrapped to a pointer and share the rest.
    const td::td_api::array<td::td_api::object_ptr<td::td_api::chatMember>> *members = nullptr;

    switch (response->get_id())
    {
        case td::td_api::chatMembers::ID:
            members = &static_cast<const td::td_api::chatMembers *>(response.get())->members_;
            break;
        case td::td_api::basicGroupFullInfo::ID:
            members = &static_cast<const td::td_api::basicGroupFullInfo *>(response.get())->members_;
            break;
        default:
            // An error, which is what a group you have just been removed from answers.
            return;
    }

    // Sorted here rather than in QML: the key is a timestamp the row never shows - it
    // shows the formatted string - so sorting over there would mean carrying both.
    std::vector<std::pair<qint64, QVariantMap>> rows;
    rows.reserve(members->size());

    for (const auto &member : *members)
    {
        auto row = formatMember(*member);

        if (!row.isEmpty())
            rows.emplace_back(row.take(QLatin1String("lastSeen")).toLongLong(), std::move(row));
    }

    std::ranges::sort(rows, std::ranges::greater(), &std::pair<qint64, QVariantMap>::first);

    m_members.clear();

    for (auto &row : rows)
        m_members.append(std::move(row.second));

    emit membersChanged();
}

QVariantMap ChatInfoFormatter::formatMember(const td::td_api::chatMember &member) const noexcept
{
    // Anonymous admins post as the chat itself, and a channel can be a member of its own
    // discussion group. Neither has a "last seen" to sort by, so neither is listed.
    if (!member.member_id_ || member.member_id_->get_id() != td::td_api::messageSenderUser::ID)
        return {};

    const auto userId = static_cast<const td::td_api::messageSenderUser *>(member.member_id_.get())->user_id_;

    const auto user = m_storageManager->user(userId);
    if (!user)
        return {};

    // The title the group gave them, or the rank itself when they never set one - the
    // same fallback the message bubbles make.
    auto tag = QString::fromStdString(member.tag_);

    if (tag.isEmpty() && member.status_)
    {
        if (member.status_->get_id() == td::td_api::chatMemberStatusCreator::ID)
            tag = tr("ChannelCreator");
        else if (member.status_->get_id() == td::td_api::chatMemberStatusAdministrator::ID)
            tag = tr("ChannelAdmin");
    }

    QVariantMap row;

    // A string, like every other id crossing into QML - and what openProfile takes when
    // the row is tapped.
    row.insert("userId", QString::number(userId));
    row.insert("name", Utils::getUserShortName(user));
    row.insert("tag", tag);
    row.insert("status", formatUserStatus(user));
    // The File itself, not a path: it is usually still to be downloaded, and the row
    // binds to the object so the avatar appears when it lands. Outlives this list either
    // way - StorageManager holds the canonical one for the whole session.
    row.insert("photo", QVariant::fromValue(user->photo()));
    row.insert("lastSeen", lastSeen(user));

    return row;
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
            newStatus = formatUserStatus(m_user);
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

QString ChatInfoFormatter::formatUserStatus(const std::shared_ptr<User> &user) const noexcept
{
    if (!user)
        return {};

    switch (user->status())
    {
        case User::Status::Empty:
            return tr("ALongTimeAgo");
        case User::Status::LastMonth:
            return tr("WithinAMonth");
        case User::Status::LastWeek:
            return tr("WithinAWeek");
        case User::Status::Offline:
            return formatOfflineStatus(user);
        case User::Status::Online:
            return tr("Online");
        case User::Status::Recently:
            return tr("Lately");
        default:
            return {};
    }
}

QString ChatInfoFormatter::formatOfflineStatus(const std::shared_ptr<User> &user) const noexcept
{
    const auto wasOnline = user->wasOnline();
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

// Hands an owned QObject to the event loop instead of destroying it here. A page being
// torn down keeps evaluating its bindings for a moment after it has been popped, so the
// context it was bound to has to outlive the call that retires it. Destroying one inline
// left the page's ListView holding a freed model. Same reason updateFolderModels()
// releases its old models rather than deleting them.
template <typename T>
void disposeLater(std::unique_ptr<T> &owner) noexcept
{
    if (auto *released = owner.release())
        released->deleteLater();
}

}  // namespace

ChatContext::ChatContext(int token, std::shared_ptr<Chat> chat, std::unique_ptr<ChatInfoFormatter> info,
                         std::unique_ptr<MessageModel> messageModel, QObject *parent)
    : QObject(parent)
    , m_token(token)
    , m_chat(std::move(chat))
    , m_info(std::move(info))
    , m_messageModel(std::move(messageModel))
{
}

int ChatContext::token() const noexcept
{
    return m_token;
}

Chat *ChatContext::chat() const noexcept
{
    return m_chat.get();
}

QString ChatContext::chatId() const noexcept
{
    return m_chat ? QString::number(m_chat->id()) : QString();
}

QObject *ChatContext::info() const noexcept
{
    return m_info.get();
}

QObject *ChatContext::messageModel() const noexcept
{
    return m_messageModel.get();
}

bool ChatContext::isReading() const noexcept
{
    return m_messageModel != nullptr;
}

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
    if (m_openChatId == 0 || (event->type() != QEvent::ApplicationActivate && event->type() != QEvent::ApplicationDeactivate))
        return QObject::eventFilter(object, event);

    // Harmattan keeps a backgrounded app alive and its window "visible" - the task
    // switcher draws a live thumbnail of it - so a chat left open stays open across a
    // minimise. TDLib takes openChat to mean the user is reading the chat: it marks
    // messages read there and suppresses their notifications, and meegramd reads it the
    // same way. Neither is true of a chat sitting in the switcher, which is exactly when
    // a banner is wanted.
    //
    // Only the TDLib side is reopened and reclosed. m_openChatId, the context stack and
    // the pages stay as they are; the user has not left the chat, they have left the app -
    // so this closes and reopens the same id rather than going through setOpenChat, which
    // would have to be told to come back to a chat it thinks is already open.
    if (event->type() == QEvent::ApplicationActivate)
        m_client->send(td::td_api::make_object<td::td_api::openChat>(m_openChatId));
    else
        m_client->send(td::td_api::make_object<td::td_api::closeChat>(m_openChatId));

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

ChatContext *ChatManager::activeContext() const noexcept
{
    for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); ++it)
    {
        if ((*it)->isReading())
            return it->get();
    }

    return nullptr;
}

QString ChatManager::activeChatId() const noexcept
{
    // Off m_openChatId rather than off activeContext(), so this and the signal that
    // notifies it always agree: setOpenChat emits from inside a push, before the new
    // context has reached the stack, and a stack scan would answer with the one underneath
    // and then never be asked again.
    return m_openChatId != 0 ? QString::number(m_openChatId) : QString();
}

void ChatManager::setOpenChat(qlonglong chatId) noexcept
{
    if (m_openChatId == chatId)
        return;

    // Closed before the next one opens, so TDLib's per-dialog open count never goes above
    // one and meegramd's single open-chat id is never left naming a chat the user has
    // moved off. Both used to be told only about opens, so stacking a chat over a chat
    // left the first counted open for the rest of the session.
    if (m_openChatId != 0)
        m_client->send(td::td_api::make_object<td::td_api::closeChat>(m_openChatId));

    m_openChatId = chatId;

    if (m_openChatId != 0)
        m_client->send(td::td_api::make_object<td::td_api::openChat>(m_openChatId));

    emit activeChatChanged(m_openChatId);
}

QObject *ChatManager::pushChat(const QString &chatId) noexcept
{
    return pushContext(chatId, true);
}

QObject *ChatManager::pushProfile(const QString &chatId) noexcept
{
    return pushContext(chatId, false);
}

ChatContext *ChatManager::pushContext(const QString &rawChatId, bool reading) noexcept
{
    const auto chatId = toId(rawChatId);

    auto chat = m_storage->chat(chatId);
    if (!chat)
    {
        // Returning null rather than failing silently. main.qml pushed ChatPage
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
        return nullptr;
    }

    // A fetch for this chat, if there was one, is done with.
    if (m_fetchingChatId == chatId)
        m_fetchingChatId = 0;

    // Before the model is built, so the chat is open by the time it asks for history:
    // TDLib treats a request against a closed chat differently, and a view reported before
    // the open has been processed does not reach the other clients. Only tell it at all
    // once the context is going to be handed out - sent earlier, a push that could not
    // happen left the server believing a chat was open that never was.
    if (reading)
        setOpenChat(chatId);

    auto info = std::make_unique<ChatInfoFormatter>(chat, m_locale, m_storage);
    auto messageModel = reading ? std::make_unique<MessageModel>(chat, m_locale, m_storage) : nullptr;

    // Parented, so the context is never a candidate for QML's collector: an object QML
    // owns goes away on the collector's schedule, and this codebase does not bet object
    // lifetime on that. popContext() is what actually frees it, and the formatter and
    // model go with it.
    auto context = std::make_unique<ChatContext>(++m_nextToken, std::move(chat), std::move(info), std::move(messageModel), this);

    auto *pushed = context.get();

    m_contextStack.push_back(std::move(context));

    return pushed;
}

void ChatManager::popContext(int token) noexcept
{
    const auto it = std::ranges::find_if(m_contextStack, [token](const auto &context) { return context->token() == token; });

    // Not ours, or retired already. A page destroyed twice must not take the context a
    // later page is using with it.
    if (it == m_contextStack.end())
        return;

    // Handed to the event loop rather than destroyed here: this runs from the page's own
    // destruction and the page keeps evaluating its bindings for a moment afterwards.
    // Destroying the model a ListView is still reading is what a segfault on leaving a
    // chat looks like.
    disposeLater(*it);

    m_contextStack.erase(it);

    // Whatever is left on screen is what is being read now. Popping the top of two stacked
    // chat pages reopens the one underneath - which used to be left bound to a torn-down
    // model with nothing open at all - and popping the last one closes the chat outright.
    const auto *active = activeContext();

    setOpenChat(active ? active->chat()->id() : 0);
}

void ChatManager::reset() noexcept
{
    // Every context goes through disposeLater for the same reason popContext uses it: a
    // page may still be evaluating bindings against one.
    for (auto &context : m_contextStack)
    {
        disposeLater(context);
    }

    m_contextStack.clear();

    // Not through setOpenChat: there is no TDLib to tell any more - a sign-out is what got
    // us here - and the request would be sent into a client that is closing.
    m_openChatId = 0;
    m_fetchingChatId = 0;

    // The models stay, and stay bound: clear() is a beginResetModel/endResetModel pair,
    // which is what every refresh already does to them.
    if (m_mainModel)
        m_mainModel->clear();

    if (m_archivedModel)
        m_archivedModel->clear();

    for (auto &model : m_folderModels)
    {
        if (model)
            model->clear();
    }

    if (m_searchModel)
        m_searchModel->clear();

    // The folder tab strip is built from what the store holds, so it empties with it. Its
    // own setter rather than a clear(): an empty vector is exactly what "no folders" is,
    // and it is what updateChatFolders would deliver for an account with none.
    if (m_folderModel)
        m_folderModel->setItems({});

    // Nothing to notify: everything QML reads off this object - mainModel, folderModel,
    // the rest - is a CONSTANT property pointing at an object that is still here and now
    // empty. The per-chat things are on ChatContext, and every context has just been
    // retired.
}

void ChatManager::openProfile(const QString &target) noexcept
{
    // A mention carries either a user id, which is also the id of the private chat with
    // them, or an @username that has to be resolved. toId gives 0 for the latter.
    const auto chatId = toId(target);

    // Already known, so nothing to resolve - the page is pushed from the answer either
    // way, and it is pushProfile() that takes the chat, not this.
    if (chatId != 0 && m_storage->chat(chatId))
    {
        emit profileReady(true, QString::number(chatId), QString());
        return;
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
    if (chatId != 0 && m_storage->chat(chatId))
    {
        emit profileReady(true, QString::number(chatId), QString());
        return;
    }

    // The two failures are worth telling apart: the request itself came back with
    // something, and separately StorageManager did or did not end up holding the chat it
    // was told about. The second one means the injected update was not taken, which is
    // the daemon-shaped bug fetchChat exists to work around.
    emit profileReady(false, QString(),
                      !reason.isEmpty() ? reason : QString::fromLatin1("chat %1 resolved but not in store").arg(chatId));
}

void ChatManager::searchMentions(const QString &query) noexcept
{
    // The chat being read, which is the one whose composer is being typed into. A profile
    // pushed over it does not change that, and nothing else on the stack has a composer.
    const auto *context = activeContext();
    const auto type = context ? context->chat()->type() : Chat::Type::None;

    // Only a group has members worth suggesting. A private chat has exactly one other
    // person and you are not going to mention them by name.
    if (type != Chat::Type::BasicGroup && type != Chat::Type::Supergroup)
    {
        emit mentionsFound({}, {}, {});
        return;
    }

    // Ten is about three screens of the panel this fills, and the query narrows fast.
    // Null filter: every member, in the order the server ranks them.
    m_client->send(td::td_api::make_object<td::td_api::searchChatMembers>(context->chat()->id(), query.toStdString(), 10, nullptr),
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

                       // Worker thread; hop before emitting. The report is the same
                       // chatAvailable a fetch by id makes, so main.qml needs nothing new to
                       // push the new group's page - but it does not go through
                       // handleChatFetched, whose latch check silently swallowed every one
                       // of these. See handleChatCreated.
                       QMetaObject::invokeMethod(this, "handleChatCreated", Qt::QueuedConnection, Q_ARG(qlonglong, chatId), Q_ARG(bool, chatId != 0));
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

void ChatManager::handleChatCreated(qlonglong chatId, bool ok) noexcept
{
    // No latch check. createGroup asks for a chat that does not exist yet, so there is no
    // id to have latched on the way out, and the request cannot be abandoned the way an
    // open of an existing chat can - the user pressed create and the group now exists.
    emit chatAvailable(QString::number(chatId), ok);
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
