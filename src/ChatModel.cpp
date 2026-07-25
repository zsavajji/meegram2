#include "ChatModel.hpp"

#include "Client.hpp"
#include "Common.hpp"
#include "Message.hpp"
#include "ScopeTimer.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QDebug>

#include <td/telegram/td_api.h>

#include <algorithm>
#include <ranges>
#include <utility>

ChatModel::ChatModel(std::unique_ptr<ChatList> list, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_list(std::move(list))
    , m_locale(std::move(locale))
    , m_storageManager(std::move(storage))
{
    connect(&m_sortTimer, SIGNAL(timeout()), this, SLOT(sortChats()));
    connect(&m_retryTimer, SIGNAL(timeout()), this, SLOT(requestMoreChats()));

    m_sortTimer.setInterval(500);
    m_sortTimer.setSingleShot(true);

    m_retryTimer.setInterval(500);
    m_retryTimer.setSingleShot(true);

    connect(m_storageManager.get(), SIGNAL(chatUpdated(qlonglong)), SLOT(handleChatItem(qlonglong)));
    connect(m_storageManager.get(), SIGNAL(chatPositionUpdated(qlonglong)), SLOT(handleChatPosition(qlonglong)));

    setRoleNames(roleNames());
}

ChatModel::~ChatModel()
{
    // Tell any in-flight loadChats callback not to touch this object.
    m_alive->store(false);
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_count;
}

const ChatModel::FormattedRow &ChatModel::formattedRow(const std::shared_ptr<Chat> &chat) const
{
    auto &entry = m_formatted[chat->id()];

    if (!entry.valid)
    {
        entry.title = Utils::getChatTitle(chat, m_storageManager);
        entry.date = Utils::getMessageDate(chat->lastMessage());
        entry.lastMessage = Utils::getContent(chat->lastMessage(), m_storageManager, m_locale);
        entry.valid = true;
    }

    return entry;
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    MEEGRAM_SCOPE("ChatModel::data");

    if (!index.isValid() || index.row() >= static_cast<int>(m_chats.size()))
        return QVariant();

    auto chatPtr = m_chats.at(index.row()).lock();
    if (!chatPtr)
        return QVariant();

    switch (role)
    {
        case IdRole:
            return chatPtr->id();
        case TypeRole:
            return chatPtr->type();
        case TitleRole:
            return formattedRow(chatPtr).title;
        case DateRole:
            return formattedRow(chatPtr).date;
        case PhotoRole:
            return QVariant::fromValue(chatPtr->photo());
        case LastMessageRole:
            return formattedRow(chatPtr).lastMessage;
        case IsPinnedRole:
        {
            // getChatPosition() returns null for a chat that is no longer in this list;
            // the previous code dereferenced it unconditionally.
            const auto *position = getChatPosition(chatPtr.get());
            return position ? position->isPinned() : false;
        }
        case UnreadCountRole:
            return chatPtr->unreadCount();
        case UnreadMentionCountRole:
            return chatPtr->unreadMentionCount();
        case IsMutedRole:
            return chatPtr->isMuted();
        case IsMarkedAsUnreadRole:
            return chatPtr->isMarkedAsUnread();
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[IdRole] = "id";
    roles[TypeRole] = "type";
    roles[TitleRole] = "title";
    roles[DateRole] = "date";
    roles[PhotoRole] = "photo";
    roles[LastMessageRole] = "lastMessage";
    roles[IsPinnedRole] = "isPinned";
    roles[UnreadCountRole] = "unreadCount";
    roles[UnreadMentionCountRole] = "unreadMentionCount";
    roles[IsMutedRole] = "isMuted";
    roles[IsMarkedAsUnreadRole] = "isMarkedAsUnread";

    return roles;
}

int ChatModel::count() const
{
    return m_count;
}

bool ChatModel::loading() const
{
    return m_loading;
}

void ChatModel::rebuildRowIndex()
{
    m_rowById.clear();
    m_rowById.reserve(static_cast<int>(m_chats.size()));

    for (int row = 0; row < static_cast<int>(m_chats.size()); ++row)
    {
        if (const auto chat = m_chats.at(row).lock())
        {
            m_rowById.insert(chat->id(), row);
        }
    }
}

void ChatModel::scheduleSort()
{
    // Coalesce bursts of reordering into one sort.
    if (!m_sortTimer.isActive())
    {
        m_sortTimer.start();
    }
}

bool ChatModel::isInList(Chat *chat) const
{
    // Presence only. An earlier version also required order != 0, on the reading that
    // TDLib uses 0 for "not in this list" - but non-pinned chats reach the model with
    // order 0 and only pinned ones carry a real order, so that hid almost everything.
    return getChatPosition(chat) != nullptr;
}

bool ChatModel::insertChatIfInList(qlonglong chatId)
{
    if (m_rowById.contains(chatId) || m_removedChatIds.contains(chatId))
        return false;

    const auto chat = m_storageManager->chat(chatId);
    if (!chat)
        return false;

    if (!isInList(chat.get()))
        return false;

    // rowCount() is m_count, and m_count <= m_chats.size() always holds, so the
    // appended chat lands outside the visible range. No row insertion has to be
    // signalled here: the pending sort places it and layoutChanged makes the view
    // re-read. Without this the model was write-once - a chat arriving after
    // populate() could never enter the list, which is why startup had to load
    // every chat before showing anything.
    m_chats.emplace_back(chat);
    m_rowById.insert(chatId, static_cast<int>(m_chats.size()) - 1);

    return true;
}

void ChatModel::requestMoreChats()
{
    if (m_requestPending || m_listFullyLoaded)
        return;

    auto client = m_storageManager->client();
    if (!client)
        return;

    auto request = td::td_api::make_object<td::td_api::loadChats>();
    request->chat_list_ = Utils::toChatList(m_list);
    request->limit_ = ChatSliceLimit;

    m_requestPending = true;

    client->send(std::move(request), [this, alive = m_alive](auto &&response) {
        // This runs on the TDLib worker thread (see Client::initialize), and the
        // model may already be gone - folder models are replaced whenever the
        // folder list changes. Bail before dereferencing anything.
        if (!alive->load())
            return;

        const auto exhausted = response->get_id() == td::td_api::error::ID &&
                               td::td_api::move_object_as<td::td_api::error>(response)->code_ == 404;

        // Hop back to the main thread before touching model state or emitting.
        // A queued call to a QObject that is destroyed first is safe: ~QObject
        // removes its pending posted events.
        QMetaObject::invokeMethod(this, "handleChatsLoaded", Qt::QueuedConnection, Q_ARG(bool, exhausted));
    });
}

void ChatModel::handleChatsLoaded(bool listExhausted)
{
    m_requestPending = false;

    // Seed once from whatever StorageManager accumulated while this batch loaded.
    // Later batches arrive through handleChatItem/handleChatPosition instead, so
    // the model is never reset out from under a scrolling view.
    if (!m_populated)
    {
        m_populated = true;
        populate();
    }

    // A 404 means "no more chats to load". On a cold cache TDLib answers that before
    // the server has pushed anything, because it genuinely knows of none locally -
    // so latching it here left the list permanently empty after a fresh install.
    // Only believe it once the model holds something; otherwise retry, which is what
    // the old 500ms loop did implicitly. Bounded, so an account with no chats at all
    // stops asking instead of polling forever.
    if (listExhausted && m_chats.empty() && m_emptyRetries < MaxEmptyRetries)
    {
        ++m_emptyRetries;
        m_retryTimer.start();

        return;  // stay in the loading state; the spinner is still correct
    }

    if (listExhausted)
    {
        m_listFullyLoaded = true;
    }

    if (m_loading)
    {
        m_loading = false;
        emit loadingChanged();
    }
}

void ChatModel::populate()
{
    // This replaces the contents wholesale, so the view has to be told. Previously
    // it mutated m_chats with no begin/endResetModel and left m_count unreconciled.
    beginResetModel();

    m_chats.clear();
    m_formatted.clear();
    m_count = 0;

    const auto &chatIds = m_storageManager->chatIds();

    m_chats.reserve(chatIds.size());

    for (const auto &id : chatIds)
    {
        auto chat = m_storageManager->chat(id);

        if (!chat)
            continue;

        if (isInList(chat.get()))
        {
            m_chats.emplace_back(chat);
        }
    }

    rebuildRowIndex();

    endResetModel();

    emit countChanged();

    // sortChats() ends in revealAll(), so the rows become visible once the order is
    // settled. On a cold cache this seeds nothing and the reveal happens instead when
    // the first chats arrive through the update slots.
    if (!m_chats.empty())
    {
        sortChats();
    }
}

void ChatModel::clear()
{
    beginResetModel();
    m_chats.clear();
    m_rowById.clear();
    m_formatted.clear();
    m_count = 0;
    endResetModel();

    emit countChanged();
}

void ChatModel::refresh()
{
    m_loading = true;
    m_populated = false;
    m_listFullyLoaded = false;
    m_emptyRetries = 0;

    clear();

    // One batch, not the whole list. Previously this started a 500ms repeating timer
    // that kept calling loadChats until TDLib answered 404 - i.e. until every chat
    // the account has was loaded, just to fill a list showing about nine rows.
    requestMoreChats();

    emit loadingChanged();
}

void ChatModel::sortChats()
{
    MEEGRAM_SCOPE("ChatModel::sortChats");

    // Decorate-sort-undecorate. std::ranges::sort re-invokes the projection on every
    // comparison, and this projection costs a weak_ptr::lock() (atomic CAS) plus a
    // linear scan of the chat's positions. Computing each key once turns
    // O(n log n) locks into O(n).
    std::vector<std::pair<qlonglong, std::weak_ptr<Chat>>> ordered;
    ordered.reserve(m_chats.size());

    for (const auto &weakChat : m_chats)
    {
        const auto chat = weakChat.lock();

        // Chats that have left this list - deleted, left, archived - used to be kept
        // and sorted to the bottom, so a deleted chat stayed on screen until restart.
        if (!chat || !isInList(chat.get()))
        {
            if (chat)
                m_formatted.remove(chat->id());

            continue;
        }

        ordered.emplace_back(getChatPosition(chat.get())->order(), weakChat);
    }

    std::ranges::sort(ordered, std::greater{}, [](const auto &entry) { return entry.first; });

    // A pure reorder can go out as layoutChanged. A shrink cannot: rowCount is about
    // to change, and only a reset lets that happen without lying to the view.
    const auto shrank = ordered.size() != m_chats.size();

    if (shrank)
        beginResetModel();
    else
        emit layoutAboutToBeChanged();

    m_chats.clear();

    for (auto &entry : ordered)
    {
        m_chats.push_back(std::move(entry.second));
    }

    rebuildRowIndex();

    if (shrank)
    {
        m_count = std::min(m_count, static_cast<int>(m_chats.size()));
        endResetModel();
        emit countChanged();
    }
    else
    {
        emit layoutChanged();
    }

    revealAll();
}

void ChatModel::revealAll()
{
    const auto total = static_cast<int>(m_chats.size());

    if (m_count >= total)
        return;

    // Everything the model holds becomes visible. The old code revealed one page and
    // relied on the view calling fetchMore() for the rest - but that is a Qt Widgets
    // view API which QML1's ListView never calls, so nothing ever grew m_count again
    // and the list froze at whatever had arrived by the first sort. Pinned chats sort
    // first, so that is exactly what stayed on screen.
    //
    // Showing them all costs nothing: a ListView only builds delegates for the rows
    // in view plus its cacheBuffer, however large rowCount is. Paging in more chats
    // from TDLib is now driven by loadMore(), which the view calls when the user
    // actually reaches the bottom.
    beginInsertRows(QModelIndex(), m_count, total - 1);
    m_count = total;
    endInsertRows();

    emit countChanged();
}

void ChatModel::loadMore()
{
    requestMoreChats();
}

void ChatModel::handleChatItem(qlonglong chatId)
{
    // updateNewChat reaches us as chatUpdated, so this is one of the two paths by
    // which a freshly loaded chat first becomes visible to the model.
    if (insertChatIfInList(chatId))
    {
        scheduleSort();
        return;
    }

    const auto it = m_rowById.constFind(chatId);
    if (it == m_rowById.constEnd())
        return;

    // Whatever changed, the cached formatting for this chat is now stale.
    m_formatted.remove(chatId);

    const auto modelIndex = createIndex(it.value(), 0);
    emit dataChanged(modelIndex, modelIndex);
}

void ChatModel::handleChatPosition(qlonglong chatId)
{
    const auto inserted = insertChatIfInList(chatId);

    if (!inserted && !m_rowById.contains(chatId))
        return;

    scheduleSort();
}

ChatPosition *ChatModel::getChatPosition(Chat *chat) const
{
    if (!chat)
        return nullptr;

    const auto &positions = chat->positions();

    if (auto it = std::ranges::find_if(positions, [&](const auto &pos) { return *pos->list() == *m_list; }); it != positions.end())
    {
        return it->get();
    }

    return nullptr;
}

void ChatModel::send(td::td_api::object_ptr<td::td_api::Function> request)
{
    if (auto client = m_storageManager->client())
    {
        client->send(std::move(request));
    }
}

void ChatModel::toggleChatIsPinned(qlonglong chatId, bool isPinned)
{
    auto request = td::td_api::make_object<td::td_api::toggleChatIsPinned>();
    request->chat_list_ = Utils::toChatList(m_list);
    request->chat_id_ = chatId;
    request->is_pinned_ = isPinned;

    send(std::move(request));
}

void ChatModel::deleteChat(qlonglong chatId)
{
    const auto chat = m_storageManager->chat(chatId);
    if (!chat)
        return;

    // Private and secret chats cannot be left; groups and channels cannot be cleared
    // out from under their other members. One menu entry, two different requests.
    if (chat->type() == Chat::Private || chat->type() == Chat::Secret)
    {
        auto request = td::td_api::make_object<td::td_api::deleteChatHistory>();
        request->chat_id_ = chatId;
        request->remove_from_chat_list_ = true;
        // Clearing our own copy only. Wiping the other side's history is a separate
        // decision and not what a swipe-to-delete style action should imply.
        request->revoke_ = false;

        send(std::move(request));
        removeChatRow(chatId);
        return;
    }

    send(td::td_api::make_object<td::td_api::leaveChat>(chatId));
    removeChatRow(chatId);
}

void ChatModel::removeChatRow(qlonglong chatId)
{
    const auto it = m_rowById.constFind(chatId);
    if (it == m_rowById.constEnd())
        return;

    const auto row = it.value();
    if (row >= static_cast<int>(m_chats.size()))
        return;

    // Rows past m_count are not part of the model as far as the view is concerned -
    // rowCount() is m_count - so only those need signalling.
    const auto visible = row < m_count;

    if (visible)
        beginRemoveRows(QModelIndex(), row, row);

    m_chats.erase(m_chats.begin() + row);
    m_removedChatIds.insert(chatId);
    m_formatted.remove(chatId);

    rebuildRowIndex();

    if (visible)
    {
        --m_count;
        endRemoveRows();
        emit countChanged();
    }
}

void ChatModel::markChatAsRead(qlonglong chatId)
{
    const auto chat = m_storageManager->chat(chatId);
    if (!chat)
        return;

    // The manual flag and genuinely unread messages are independent - a chat can have
    // both - so clearing one does not clear the other.
    if (chat->isMarkedAsUnread())
    {
        send(td::td_api::make_object<td::td_api::toggleChatIsMarkedAsUnread>(chatId, false));
    }

    if (chat->unreadCount() > 0)
    {
        if (const auto *message = chat->lastMessage())
        {
            auto request = td::td_api::make_object<td::td_api::viewMessages>();
            request->chat_id_ = chatId;
            request->message_ids_ = {message->id()};
            request->force_read_ = true;

            send(std::move(request));
        }
    }
}

void ChatModel::markChatAsUnread(qlonglong chatId)
{
    send(td::td_api::make_object<td::td_api::toggleChatIsMarkedAsUnread>(chatId, true));
}

void ChatModel::setChatMuted(qlonglong chatId, bool muted)
{
    auto settings = td::td_api::make_object<td::td_api::chatNotificationSettings>();

    // setChatNotificationSettings replaces the whole object, so every field left at
    // its default would silently override that setting for this chat. Only mute_for
    // is set here; the rest stay on the scope default they already inherit.
    settings->use_default_mute_for_ = false;
    settings->mute_for_ = muted ? MutedValueMax : MutedValueMin;
    settings->use_default_sound_ = true;
    settings->use_default_show_preview_ = true;
    settings->use_default_disable_pinned_message_notifications_ = true;
    settings->use_default_disable_mention_notifications_ = true;

    auto request = td::td_api::make_object<td::td_api::setChatNotificationSettings>();
    request->chat_id_ = chatId;
    request->notification_settings_ = std::move(settings);

    send(std::move(request));
}

