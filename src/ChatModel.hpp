#pragma once

#include "Chat.hpp"
#include "ChatPosition.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

class Locale;
class StorageManager;

class ChatModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit ChatModel(std::unique_ptr<ChatList> list, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);
    ~ChatModel() override;

    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        TitleRole,
        PhotoRole,
        DateRole,
        LastMessageRole,
        IsPinnedRole,
        UnreadMentionCountRole,
        UnreadCountRole,
        IsMutedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool canFetchMore(const QModelIndex &parent = QModelIndex()) const override;
    void fetchMore(const QModelIndex &parent = QModelIndex()) override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const;

    int count() const;
    bool loading() const;

    Q_INVOKABLE void toggleChatIsPinned(qlonglong chatId, bool isPinned);

    Q_INVOKABLE ChatPosition *getChatPosition(Chat *chat) const;

signals:
    void countChanged();
    void loadingChanged();

public slots:
    void clear();
    void populate();
    void refresh();

private slots:
    void sortChats();

    // Invoked (queued) from the loadChats callback, which runs on the TDLib worker
    // thread - see the note in requestMoreChats().
    void handleChatsLoaded(bool listExhausted);

    void handleChatItem(qlonglong chatId);
    void handleChatPosition(qlonglong chatId);

private:
    // The three roles whose values cost a formatter call. QML1 has no per-row role
    // cache, so a delegate reading 8 properties calls data() 8 times; without this
    // every one of those reads re-ran getChatTitle/getMessageDate/getContent.
    struct FormattedRow
    {
        QString title;
        QString date;
        QString lastMessage;
        bool valid{false};
    };

    void rebuildRowIndex();
    void scheduleSort();
    void requestMoreChats();

    // Adds a chat that has appeared in this list but is not in the model yet.
    // Returns true if it was inserted.
    bool insertChatIfInList(qlonglong chatId);

    const FormattedRow &formattedRow(const std::shared_ptr<Chat> &chat) const;

    bool m_loading{true};

    // populate() seeds the model from whatever StorageManager already holds; it runs
    // once per refresh, after which chats arrive incrementally via the update slots.
    bool m_populated{false};

    // Guards against stacking loadChats requests, and stops asking once TDLib has
    // reported the list exhausted.
    bool m_requestPending{false};
    bool m_listFullyLoaded{false};

    int m_count{};

    QTimer m_sortTimer;

    std::unique_ptr<ChatList> m_list;

    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storageManager;

    std::vector<std::weak_ptr<Chat>> m_chats;

    // chatId -> row in m_chats. Replaces a linear scan that called weak_ptr::lock()
    // (an atomic CAS) on every element to resolve one update.
    QHash<qlonglong, int> m_rowById;

    // Keyed by chatId, so reordering the list does not invalidate it.
    mutable QHash<qlonglong, FormattedRow> m_formatted;

    // Liveness token for the loadChats callback, which Client invokes on the TDLib
    // worker thread. Folder models are destroyed whenever the folder list changes,
    // so a response can outlive the model that asked for it.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
};
