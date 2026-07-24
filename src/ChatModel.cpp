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
#include <limits>
#include <ranges>
#include <utility>

ChatModel::ChatModel(std::unique_ptr<ChatList> list, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_list(std::move(list))
    , m_locale(std::move(locale))
    , m_storageManager(std::move(storage))
{
    connect(&m_sortTimer, SIGNAL(timeout()), this, SLOT(sortChats()));

    m_sortTimer.setInterval(500);
    m_sortTimer.setSingleShot(true);

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

bool ChatModel::canFetchMore(const QModelIndex &parent) const
{
    if (parent.isValid())
        return false;

    return m_count < static_cast<int>(m_chats.size());
}

void ChatModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid())
        return;

    int remainingItems = static_cast<int>(m_chats.size()) - m_count;

    const auto itemsToFetch = std::min(ChatSliceLimit, remainingItems);

    if (itemsToFetch <= 0)
        return;

    int startIndex = m_count;
    int endIndex = m_count + itemsToFetch - 1;

    beginInsertRows(QModelIndex(), startIndex, endIndex);
    m_count += itemsToFetch;
    endInsertRows();

    emit countChanged();

    // The view has now consumed everything TDLib has handed us, so pull the next
    // page. This is what replaces the old "load the entire chat list at startup"
    // loop: chats are requested as the user actually reaches them.
    if (m_count >= static_cast<int>(m_chats.size()))
    {
        requestMoreChats();
    }
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

bool ChatModel::insertChatIfInList(qlonglong chatId)
{
    if (m_rowById.contains(chatId))
        return false;

    const auto chat = m_storageManager->chat(chatId);
    if (!chat)
        return false;

    if (!std::ranges::any_of(chat->positions(), [&](const auto &pos) { return *pos->list() == *m_list; }))
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

    if (listExhausted)
    {
        m_listFullyLoaded = true;
    }

    // Seed once from whatever StorageManager accumulated while this batch loaded.
    // Later batches arrive through handleChatItem/handleChatPosition instead, so
    // the model is never reset out from under a scrolling view.
    if (!m_populated)
    {
        m_populated = true;
        populate();
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

        if (std::ranges::any_of(chat->positions(), [&](const auto &pos) { return *pos->list() == *m_list; }))
        {
            m_chats.emplace_back(chat);
        }
    }

    rebuildRowIndex();

    endResetModel();

    emit countChanged();

    // sortChats() reveals the first page itself once the order is settled, so there
    // is deliberately no fetchMore() call here - doing both would hand out two pages.
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

    emit layoutAboutToBeChanged();

    // Decorate-sort-undecorate. std::ranges::sort re-invokes the projection on every
    // comparison, and this projection costs a weak_ptr::lock() (atomic CAS) plus a
    // linear scan of the chat's positions. Computing each key once turns
    // O(n log n) locks into O(n).
    std::vector<std::pair<qlonglong, std::weak_ptr<Chat>>> ordered;
    ordered.reserve(m_chats.size());

    for (const auto &weakChat : m_chats)
    {
        auto order = std::numeric_limits<qlonglong>::min();

        if (const auto chat = weakChat.lock())
        {
            if (const auto *position = getChatPosition(chat.get()))
            {
                order = position->order();
            }
        }

        ordered.emplace_back(order, weakChat);
    }

    std::ranges::sort(ordered, std::greater{}, [](const auto &entry) { return entry.first; });

    m_chats.clear();

    for (auto &entry : ordered)
    {
        m_chats.push_back(std::move(entry.second));
    }

    rebuildRowIndex();

    emit layoutChanged();

    // On a cold start populate() sees an empty cache, so the first chats to arrive
    // land beyond the visible range. Reveal them here or the list stays blank until
    // the user scrolls.
    if (m_count == 0 && !m_chats.empty())
    {
        fetchMore();
    }
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

void ChatModel::toggleChatIsPinned(qlonglong chatId, bool isPinned)
{
    auto request = td::td_api::make_object<td::td_api::toggleChatIsPinned>();
    request->chat_list_ = Utils::toChatList(m_list);
    request->chat_id_ = chatId;
    request->is_pinned_ = isPinned;

    auto client = m_storageManager->client();
    if (!client)
        return;

    client->send(std::move(request));
}

