#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariant>

#include <atomic>
#include <memory>
#include <vector>

class Chat;
class StorageManager;
class User;

// People and public groups that are not in the chat list yet, for starting a conversation
// with them. ChatModel filters chats TDLib has already pushed; this one asks the server
// and holds what comes back.
//
// A real model rather than a list of maps handed to QML, because a row is not a snapshot:
// a person found here is usually somebody whose avatar was never in the cache, so the
// photo arrives after the row does. Holding the Chat and the User keeps the File that the
// delegate binds to alive and current - the same arrangement ChatModel has.
class SearchModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

    // Whether an answer has come back at all. "Nothing matched" and "nothing has been
    // asked yet" are the same empty model, and only one of them is worth a message.
    Q_PROPERTY(bool searched READ searched NOTIFY searchedChanged)

    // How many rows are ticked, for the member picker. Kept here rather than in QML because
    // filtering the list rebuilds every delegate, and a tick that lives in a delegate is
    // lost the moment it scrolls or the query changes.
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedCountChanged)

public:
    explicit SearchModel(std::shared_ptr<StorageManager> storage);
    ~SearchModel() override;

    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        UsernameRole,
        PhotoRole,
        SelectedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const;

    int count() const noexcept;
    int selectedCount() const noexcept;
    bool loading() const noexcept;
    bool searched() const noexcept;

    // Contacts by name or username, and everybody else by public username. An empty query
    // just clears the model.
    Q_INVOKABLE void search(const QString &query);

    // Contacts only, and an empty query lists them all - the member picker, where a channel
    // found by username would be nonsense. Deliberately a bigger slice than search() takes:
    // that one shows a handful of contacts beside the public hits, this one is the list you
    // are choosing from.
    Q_INVOKABLE void searchContacts(const QString &query);

    // The picker's ticks. Held by id rather than by row, so filtering the list keeps them:
    // pick somebody, search for the next person, and the first is still chosen.
    Q_INVOKABLE void toggleSelection(const QString &id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QStringList selectedIds() const;

    // Sends the phone's address book to Telegram and fills the model with the people in it
    // who have an account. Not a lookup: the numbers are uploaded and the matches are added
    // to this account's contact list on the server, the way the official clients do it - so
    // only ever call it from a confirmed action.
    Q_INVOKABLE void importPhoneContacts();

signals:
    void countChanged();
    void loadingChanged();
    void searchedChanged();
    void selectedCountChanged();

private slots:
    // Invoked (queued) from the TDLib worker thread - see the note in sendSearch().
    void handleResults(int requestId, const QVariantList &chatIds);

private:
    struct Row
    {
        qlonglong id{};

        // One or the other. A contact who has never been written to has no chat, which is
        // the whole reason this model does not just hold chats.
        std::shared_ptr<Chat> chat;
        std::shared_ptr<User> user;
    };

    // Starts a new query: bumps the request id so slower answers to the last one are
    // dropped, and empties what is on screen.
    int beginRequest(int pendingResponses);

    void setLoading(bool loading);

    // The contacts half of both entry points. Its answer lands in handleResults like any
    // other, so the two differ only in what else they ask for.
    void sendContactSearch(const QString &query, int limit, int requestId);

    // Asks TDLib to fetch an avatar that is not on disk yet. The delegate binds to the
    // File, which notifies on its own when the download lands, so nothing here has to
    // watch for it.
    void ensurePhotoDownloaded(const Row &row) const;

    std::shared_ptr<StorageManager> m_storage;

    std::vector<Row> m_rows;

    bool m_loading{false};
    bool m_searched{false};

    // Typing "and" then "andr" is two requests, and TDLib answers them in whatever order
    // it likes. Only the newest is allowed to reach the model.
    int m_requestId{0};

    // One search is two requests - contacts and public usernames - whose answers are
    // merged as they land, so how many are still outstanding decides whether an empty
    // model is "nothing matched" or "not finished".
    int m_pending{0};

    QSet<qlonglong> m_seenIds;

    // Survives a new query on purpose - see toggleSelection. Emptied only by
    // clearSelection(), which the picker calls on its way in.
    QSet<qlonglong> m_selected;

    // Liveness token for the search callbacks, which Client invokes on the TDLib worker
    // thread. ChatManager owns this model and is destroyed on sign-out, so an answer can
    // outlive the model that asked for it.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
};
