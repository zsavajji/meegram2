#include "ForumTopicModel.hpp"

#include "Chat.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "Message.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <iterator>

namespace {

// One page of topics. A forum a phone is reading has tens of them, so this is usually
// the whole list in one request.
constexpr int TopicSliceLimit = 20;

// What Telegram gives a topic whose icon is a plain coloured circle rather than a custom
// emoji. The server sends one of these in forumTopicIcon.color; the fallback below is for
// the topics that arrive with no colour at all, picked by id so a topic keeps its own.
constexpr int DefaultIconColors[] = {0x6FB9F0, 0xFFD67E, 0xCB86DB, 0x8EEE98, 0xFF93B2, 0xFB6F5F};

int iconColorFor(const td::td_api::forumTopicInfo &info)
{
    if (info.icon_ && info.icon_->color_ != 0)
        return info.icon_->color_;

    return DefaultIconColors[static_cast<unsigned>(info.forum_topic_id_) % std::size(DefaultIconColors)];
}

}  // namespace

ForumTopicModel::ForumTopicModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_client(storage->client())
    , m_locale(std::move(locale))
    , m_storage(std::move(storage))
    , m_chat(std::move(chat))
{
    connect(m_client.get(), SIGNAL(result(td::td_api::Object *)), SLOT(handleResult(td::td_api::Object *)));

    setRoleNames(roleNames());

    // Deliberately not subscribed to updateNewMessage, so a message arriving in a topic
    // does not move that row's preview or its unread count. Two reasons, and the second
    // is the real one: the page is re-read on every return to it (TopicListPage calls
    // refresh when it becomes active again), and the update's message cannot be taken -
    // Client::dispatch frees it when the last subscriber returns, td_api objects have no
    // copy, and moving it out would steal it from the MessageModel of a topic page open
    // on top of this list.
    requestTopics(false);
}

ForumTopicModel::~ForumTopicModel()
{
    // Tell any in-flight getForumTopics callback not to touch this object.
    m_alive->store(false);
}

int ForumTopicModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return static_cast<int>(m_rows.size());
}

QVariant ForumTopicModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return QVariant();

    const auto &row = m_rows.at(index.row());

    switch (role)
    {
        case IdRole:
            return row.id;
        case NameRole:
            return row.name;
        case IconColorRole:
            return row.iconColor;
        case LastMessageRole:
            return formattedRow(index.row()).preview;
        case DateRole:
            return formattedRow(index.row()).date;
        case UnreadCountRole:
            return row.unreadCount;
        case UnreadMentionCountRole:
            return row.unreadMentionCount;
        case IsPinnedRole:
            return row.isPinned;
        case IsClosedRole:
            return row.isClosed;
        case IsGeneralRole:
            return row.isGeneral;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> ForumTopicModel::roleNames() const
{
    // Filled key by key: Qt 4's QHash has no initializer-list constructor, the same trap
    // CLAUDE.md's rule 12 names for QList. Same shape as SearchModel::roleNames.
    QHash<int, QByteArray> roles;

    roles[IdRole] = "id";
    roles[NameRole] = "name";
    roles[IconColorRole] = "iconColor";
    roles[LastMessageRole] = "lastMessage";
    roles[DateRole] = "date";
    roles[UnreadCountRole] = "unreadCount";
    roles[UnreadMentionCountRole] = "unreadMentionCount";
    roles[IsPinnedRole] = "isPinned";
    roles[IsClosedRole] = "isClosed";
    roles[IsGeneralRole] = "isGeneral";

    return roles;
}

int ForumTopicModel::count() const noexcept
{
    return static_cast<int>(m_rows.size());
}

bool ForumTopicModel::loading() const noexcept
{
    return m_loading;
}

void ForumTopicModel::loadMore()
{
    requestTopics(true);
}

void ForumTopicModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();

    m_listFullyLoaded = false;
    m_nextOffsetDate = 0;
    m_nextOffsetMessageId = 0;
    m_nextOffsetTopicId = 0;

    // Anything already out belongs to the list that was just emptied.
    ++m_requestId;
    m_requestPending = false;

    emit countChanged();

    requestTopics(false);
}

void ForumTopicModel::requestTopics(bool more)
{
    if (m_requestPending || (more && m_listFullyLoaded))
        return;

    m_requestPending = true;
    setLoading(true);

    auto request = td::td_api::make_object<td::td_api::getForumTopics>();

    request->chat_id_ = m_chat->id();
    request->query_ = std::string();
    request->limit_ = TopicSliceLimit;

    // A first request asks from the start; paging continues from where the last answer
    // said to. Unlike getChats this really is an offset, so a page is a page.
    if (more)
    {
        request->offset_date_ = m_nextOffsetDate;
        request->offset_message_id_ = m_nextOffsetMessageId;
        request->offset_forum_topic_id_ = m_nextOffsetTopicId;
    }

    m_client->send(std::move(request), [this, requestId = m_requestId, alive = m_alive](auto &&response) {
        // Runs on the TDLib worker thread, and the model may already be gone: leaving the
        // topic list destroys it while this is still out.
        if (!alive->load())
            return;

        // Nothing else is touched here. Same hop and the same void* handover as
        // MessageModel's history reply, and for the same reason: everything below writes
        // containers the GUI thread reads in data().
        QMetaObject::invokeMethod(this, "handleTopics", Qt::QueuedConnection, Q_ARG(int, requestId), Q_ARG(void *, response.release()));
    });
}

void ForumTopicModel::handleTopics(int requestId, void *responseObject) noexcept
{
    // Ownership arrives here, on the GUI thread - taken before the staleness test below,
    // so an abandoned answer is freed rather than leaked.
    td::td_api::object_ptr<td::td_api::Object> response(static_cast<td::td_api::Object *>(responseObject));

    if (requestId != m_requestId)
        return;

    m_requestPending = false;
    setLoading(false);

    if (!response || response->get_id() != td::td_api::forumTopics::ID)
        return;

    // Non-const: this response is ours alone, so each topic's last message is moved out
    // of it rather than copied - td_api objects hold unique_ptrs and have no copy.
    auto &topics = static_cast<td::td_api::forumTopics &>(*response);

    m_nextOffsetDate = topics.next_offset_date_;
    m_nextOffsetMessageId = topics.next_offset_message_id_;
    m_nextOffsetTopicId = topics.next_offset_forum_topic_id_;

    // A short page is the end of the list. The server also stops moving its offsets,
    // so this is belt and braces against asking forever.
    if (static_cast<int>(topics.topics_.size()) < TopicSliceLimit)
        m_listFullyLoaded = true;

    std::vector<Row> incoming;

    for (auto &topic : topics.topics_)
    {
        if (!topic || !topic->info_)
            continue;

        // Already held: a page re-requested after a refresh must not double the list.
        if (indexOfTopic(topic->info_->forum_topic_id_) >= 0)
            continue;

        Row row;

        row.id = topic->info_->forum_topic_id_;
        row.name = QString::fromStdString(topic->info_->name_);
        row.iconColor = iconColorFor(*topic->info_);
        row.isGeneral = topic->info_->is_general_;
        row.isClosed = topic->info_->is_closed_;
        row.isPinned = topic->is_pinned_;
        row.unreadCount = topic->unread_count_;
        row.unreadMentionCount = topic->unread_mention_count_;
        row.lastReadInboxMessageId = topic->last_read_inbox_message_id_;

        if (topic->last_message_)
            row.lastMessage = std::make_unique<Message>(std::move(topic->last_message_));

        incoming.push_back(std::move(row));
    }

    if (incoming.empty())
        return;

    // Appended in the order the server gave them, which is pinned first and then by the
    // topic's own order - so this model never sorts. The one thing that follows is that a
    // topic receiving a message keeps its place until the page is reopened.
    //
    // ponytail: no re-sort on activity. getForumTopics answers with an order per topic;
    // sort on it if a busy forum ever makes the fixed order annoying.
    const int first = static_cast<int>(m_rows.size());

    beginInsertRows(QModelIndex(), first, first + static_cast<int>(incoming.size()) - 1);

    for (auto &row : incoming)
        m_rows.push_back(std::move(row));

    endInsertRows();

    emit countChanged();
}

void ForumTopicModel::handleResult(td::td_api::Object *object) noexcept
{
    switch (object->get_id())
    {
        case td::td_api::updateForumTopicInfo::ID: {
            auto *update = static_cast<td::td_api::updateForumTopicInfo *>(object);

            if (update->info_ && update->info_->chat_id_ == m_chat->id())
                handleTopicInfo(*update->info_);

            break;
        }
        case td::td_api::updateForumTopic::ID: {
            auto *update = static_cast<td::td_api::updateForumTopic *>(object);

            if (update->chat_id_ == m_chat->id())
                handleTopicChanged(*update);

            break;
        }
        default:
            break;
    }
}

void ForumTopicModel::handleTopicInfo(const td::td_api::forumTopicInfo &info)
{
    const int index = indexOfTopic(info.forum_topic_id_);

    if (index < 0)
        return;

    auto &row = m_rows[index];

    row.name = QString::fromStdString(info.name_);
    row.iconColor = iconColorFor(info);
    row.isClosed = info.is_closed_;

    const auto modelIndex = createIndex(index, 0);

    emit dataChanged(modelIndex, modelIndex);
}

void ForumTopicModel::handleTopicChanged(const td::td_api::updateForumTopic &update)
{
    const int index = indexOfTopic(update.forum_topic_id_);

    if (index < 0)
        return;

    auto &row = m_rows[index];

    row.isPinned = update.is_pinned_;
    row.unreadMentionCount = update.unread_mention_count_;
    row.lastReadInboxMessageId = update.last_read_inbox_message_id_;

    // This update carries no unread_count of its own, only the read pointer - so the
    // count is cleared when the pointer catches up with the last message this row knows
    // about, which is what reading the topic (here or on another client) does.
    //
    // ponytail: between that and a refresh the count is the server's figure plus whatever
    // has arrived since, not a recomputation. Ask getForumTopic for the row if it drifts
    // somewhere visible.
    if (row.lastMessage && update.last_read_inbox_message_id_ >= row.lastMessage->id())
        row.unreadCount = 0;

    const auto modelIndex = createIndex(index, 0);

    emit dataChanged(modelIndex, modelIndex);
}

const ForumTopicModel::Row &ForumTopicModel::formattedRow(int row) const
{
    const auto &entry = m_rows.at(row);

    if (!entry.formatted)
    {
        entry.date = Utils::getMessageDate(entry.lastMessage.get());
        entry.preview = Utils::getContent(entry.lastMessage.get(), m_storage, m_locale);

        // One elided line, so the breaks have to go - the same reason and the same place
        // ChatModel strips them, which is to say not in the delegate.
        entry.preview.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));

        entry.formatted = true;
    }

    return entry;
}

int ForumTopicModel::indexOfTopic(int topicId) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].id == topicId)
            return static_cast<int>(i);
    }

    return -1;
}

void ForumTopicModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;

    m_loading = loading;

    emit loadingChanged();
}
