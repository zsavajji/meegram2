#pragma once

#include <td/telegram/td_api.h>

#include <QAbstractListModel>

#include <atomic>
#include <memory>
#include <vector>

class Chat;
class Client;
class Locale;
class Message;
class StorageManager;

// The topics of one forum supergroup: the list a forum opens into instead of a message
// list. Tapping a row pushes an ordinary ChatPage whose MessageModel is scoped to that
// topic - see MessageModel's forumTopicId.
//
// A sibling of ChatModel rather than a subclass of it. A topic looks like a chat row on
// screen and is nothing like one underneath: it has no ChatList, no position, no photo
// and no presence in StorageManager, and ChatModel's whole body is about those four
// things. The two share their geometry through the painted row, not through code.
class ForumTopicModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit ForumTopicModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);
    ~ForumTopicModel() override;

    enum Roles {
        // The forum topic id. An int32 in td_api - unlike every other id in this codebase
        // it fits in one, so it crosses into QML as a number without the string dance the
        // note in Chat.hpp describes. It still goes back as a string, because every
        // Q_INVOKABLE here takes ids that way and one convention is worth more than one
        // saved conversion.
        IdRole = Qt::UserRole + 1,
        NameRole,
        // The topic's icon colour as an 0xRRGGBB int, already defaulted - see iconColor().
        IconColorRole,
        LastMessageRole,
        DateRole,
        UnreadCountRole,
        UnreadMentionCountRole,
        IsPinnedRole,
        IsClosedRole,
        IsGeneralRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const;

    int count() const noexcept;
    bool loading() const noexcept;

    // The view's "I have reached the bottom". QML1's ListView does not use
    // canFetchMore/fetchMore, so demand has to come from QML explicitly - the same
    // arrangement as ChatModel::loadMore.
    Q_INVOKABLE void loadMore();

signals:
    void countChanged();
    void loadingChanged();

public slots:
    void refresh();

private slots:
    void handleResult(td::td_api::Object *object) noexcept;

    // The getForumTopics reply, handed over from the TDLib worker thread. Takes ownership
    // of the raw pointer; void* because a queued Q_ARG needs a registered metatype and
    // td_api::object_ptr is move-only - the same handover
    // MessageModel::handleHistoryResponse documents, and for the same reason.
    void handleTopics(int requestId, void *responseObject) noexcept;

private:
    struct Row
    {
        int id{};
        QString name;
        int iconColor{};
        int unreadCount{};
        int unreadMentionCount{};
        qlonglong lastReadInboxMessageId{};
        bool isPinned{false};
        bool isClosed{false};
        bool isGeneral{false};

        // Owned here: a topic's last message arrives inside the topic object and is not
        // registered anywhere, so nothing else is holding it.
        std::unique_ptr<Message> lastMessage;

        // The two formatter calls a row costs, cached for the same reason
        // ChatModel::FormattedRow is: QML1 has no per-row role cache, so a row reading
        // six roles calls data() six times, and without this both of these would re-run
        // on every one of them.
        //
        // mutable, so the cache can be filled from data(), which is const - the same
        // thing ChatModel's `mutable` formatting map does, kept inside the row here
        // because the values die with it.
        mutable QString date;
        mutable QString preview;
        mutable bool formatted{false};
    };

    void requestTopics(bool more);

    // Fills date and preview on first read, and is reset by anything that replaces the
    // row's last message.
    const Row &formattedRow(int row) const;

    // The row holding a topic id, or -1. A forum a phone is reading has tens of topics,
    // not thousands, so a scan is the whole lookup and there is no index to keep in step.
    int indexOfTopic(int topicId) const;

    void setLoading(bool loading);

    void handleTopicInfo(const td::td_api::forumTopicInfo &info);
    void handleTopicChanged(const td::td_api::updateForumTopic &update);

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storage;
    std::shared_ptr<Chat> m_chat;

    std::vector<Row> m_rows;

    bool m_loading{true};

    // Guards against stacking requests, and stops asking once the server has said there
    // is no more. getForumTopics does take an offset, so paging here is a real
    // continuation rather than ChatModel's lengthening prefix.
    bool m_requestPending{false};
    bool m_listFullyLoaded{false};

    // Bumped by refresh(), so a page still in flight when the list is emptied cannot land
    // in the fresh model and leave it holding only page two. Same guard, and the same
    // reason, as SearchModel's m_requestId.
    int m_requestId{0};

    int m_nextOffsetDate{0};
    qlonglong m_nextOffsetMessageId{0};
    int m_nextOffsetTopicId{0};

    // Liveness token for the getForumTopics callback, which Client invokes on the TDLib
    // worker thread. The model dies with the page that pushed it, and a reply can outlive
    // it. Same guard and same reason as MessageModel's.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
};
