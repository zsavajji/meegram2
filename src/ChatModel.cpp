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
        // calls = rows formatted from scratch, against ChatModel::data's calls for the
        // hit rate. Included in data's total, so never add the two rows together.
        MEEGRAM_SCOPE("ChatModel::formattedRow.miss");

        entry.title = Utils::getChatTitle(chat, m_storageManager);

        // Measured at 380us a call and it used to run from a QML binding in the
        // delegate, so it re-ran on every rebind even when this cache took no misses:
        // 233 calls and 88.5ms during a 60s warm scroll, against 9.6ms for every
        // data() call in the same window (docs/profiling.md). Here it runs once a row.
        entry.titleHtml = Utils::replaceEmoji(entry.title);

        entry.date = Utils::getMessageDate(chat->lastMessage());
        entry.lastMessage = Utils::getContent(chat->lastMessage(), m_storageManager, m_locale);

        // The preview is one elided line, so the line breaks have to go. Here rather
        // than in the delegate, where it was two JS regex passes over the string every
        // time the view built a row.
        entry.lastMessage.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));

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
        case TitleHtmlRole:
            return formattedRow(chatPtr).titleHtml;
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
    roles[TitleHtmlRole] = "titleHtml";
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

QString ChatModel::filter() const
{
    return m_filter;
}

void ChatModel::setFilter(const QString &filter)
{
    const auto trimmed = filter.trimmed();

    if (trimmed == m_filter)
        return;

    m_filter = trimmed;
    emit filterChanged();

    // Re-scan from storage rather than narrowing what is already here: widening or
    // clearing the filter has to bring rows back, and storage is the only place that
    // still knows about them.
    populate();

    // The list is paged - 25 chats a batch, more only when the user scrolls to the
    // bottom - so an unqualified search would only ever see the first screenful.
    // Pull the rest in while a search is running; handleChatsLoaded chains the next
    // batch until TDLib says the list is exhausted.
    if (!m_filter.isEmpty())
        requestMoreChats();
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

bool ChatModel::matchesFilter(const std::shared_ptr<Chat> &chat) const
{
    if (m_filter.isEmpty())
        return true;

    // formattedRow rather than a fresh getChatTitle: the title is cached per chat id,
    // so the second keystroke onwards costs a string compare per chat.
    //
    // ponytail: title only. Searching message text means a TDLib searchMessages call
    // and a second model; add that if title search turns out not to be what people
    // reach for here.
    return formattedRow(chat).title.contains(m_filter, Qt::CaseInsensitive);
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

    // A chat arriving while a search is running only joins the list if it matches -
    // which is also how the batches pulled in by setFilter reach the results.
    if (!matchesFilter(chat))
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

    // A search wants the whole list, not the first page. Chains one batch at a time
    // until TDLib reports the list exhausted; stops as soon as the field is cleared,
    // so the unfiltered list still pages only when the user scrolls.
    //
    // ponytail: unbounded - an account with thousands of chats loads all of them for
    // one search. Cap the number of chained batches if that turns out to hurt on
    // device; the alternative is a searchChats request and a second model.
    if (!m_filter.isEmpty() && !m_listFullyLoaded)
        requestMoreChats();
}

void ChatModel::populate()
{
    // This replaces the contents wholesale, so the view has to be told. Previously
    // it mutated m_chats with no begin/endResetModel and left m_count unreconciled.
    beginResetModel();

    m_chats.clear();
    m_count = 0;

    // m_formatted is deliberately kept: it is keyed by chat id and invalidated per
    // chat on update, and a search re-populates on every keystroke - clearing it here
    // meant re-running getChatTitle and replaceEmoji (380us a chat) for the whole
    // list each time. clear() still wipes it on refresh.

    const auto &chatIds = m_storageManager->chatIds();

    m_chats.reserve(chatIds.size());

    for (const auto &id : chatIds)
    {
        auto chat = m_storageManager->chat(id);

        if (!chat)
            continue;

        if (isInList(chat.get()) && matchesFilter(chat))
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

    // Without this, a refresh() while a loadChats is still in flight is dropped by the
    // guard in requestMoreChats() and m_loading never clears - the spinner stays up
    // forever.
    // ponytail: lets a second loadChats overlap the in-flight one. Both land in
    // handleChatsLoaded, m_populated guards the seed, and the stale callback repopulates
    // the cleared model - which is what refresh wanted anyway. Track a request id if
    // overlapping requests ever start costing something.
    m_requestPending = false;

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
        if (!chat)
            continue;

        if (!isInList(chat.get()))
        {
            m_formatted.remove(chat->id());
            continue;
        }

        // Filtered out, not gone: keep the cached title, it is what the next
        // keystroke matches against.
        if (!matchesFilter(chat))
            continue;

        ordered.emplace_back(getChatPosition(chat.get())->order(), weakChat);
    }

    std::ranges::sort(ordered, std::greater{}, [](const auto &entry) { return entry.first; });

    // Most position updates do not actually move anything: another message in the chat
    // that is already at the top, a read receipt, a mute. Every one of them still went
    // out as layoutChanged, and a QML1 ListView answers that by re-reading every
    // delegate it has built and laying the list out again - on a busy account, every
    // 500ms, including under a finger that is mid-flick. So compare before signalling.
    //
    // Owner-equivalence rather than lock()-and-compare: it is the whole point of the
    // decorate step above not to pay an atomic per element.
    const auto sameOrder = std::ranges::equal(
        ordered, m_chats, [](const auto &lhs, const auto &rhs) { return !lhs.owner_before(rhs) && !rhs.owner_before(lhs); },
        [](const auto &entry) -> const auto & { return entry.second; });

    if (sameOrder)
    {
        // Nothing moved, but chats appended by insertChatIfInList are still waiting
        // behind m_count, and this is what puts them on screen.
        revealAll();
        return;
    }

    // calls = sorts that actually signalled the view, against ChatModel::sortChats for
    // the ones the sameOrder check swallowed.
    MEEGRAM_SCOPE("ChatModel::sortChats.layout");

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

void ChatModel::toggleChatIsPinned(const QString &rawChatId, bool isPinned)
{
    const auto chatId = toId(rawChatId);

    auto request = td::td_api::make_object<td::td_api::toggleChatIsPinned>();
    request->chat_list_ = Utils::toChatList(m_list);
    request->chat_id_ = chatId;
    request->is_pinned_ = isPinned;

    send(std::move(request));
}

void ChatModel::deleteChat(const QString &rawChatId)
{
    const auto chatId = toId(rawChatId);

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

void ChatModel::markChatAsRead(const QString &rawChatId)
{
    const auto chatId = toId(rawChatId);

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

void ChatModel::markChatAsUnread(const QString &rawChatId)
{
    const auto chatId = toId(rawChatId);

    send(td::td_api::make_object<td::td_api::toggleChatIsMarkedAsUnread>(chatId, true));
}

void ChatModel::setChatMuted(const QString &rawChatId, bool muted)
{
    const auto chatId = toId(rawChatId);

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

