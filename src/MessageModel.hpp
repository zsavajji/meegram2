#pragma once

#include <td/telegram/td_api.h>

#include <QAbstractListModel>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Chat;
class ChatManager;
class Client;
class Locale;
class Message;
class StorageManager;

class MessageModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool backFetching READ backFetching NOTIFY backFetchingChanged)

public:
    explicit MessageModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);

    enum Role {
        IdRole = Qt::UserRole + 1,
        SenderRole,
        ChatIdRole,
        IsOutgoingRole,
        DateRole,
        EditDateRole,
        ContentRole,
        // Custom role
        ContentTypeRole,
        IsServiceRole,
        ServiceMessageRole,
        SectionRole,
        ReplyToSenderRole,
        ReplyToTextRole
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool canFetchMore(const QModelIndex &parent = QModelIndex()) const override;
    void fetchMore(const QModelIndex &parent = QModelIndex()) override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const noexcept;

    int count() const noexcept;
    bool loading() const noexcept;
    bool backFetching() const noexcept;

    Q_INVOKABLE void getChatHistory(qlonglong fromMessageId, int offset, int limit, bool fetchPrevious = false) noexcept;
    // Reports the newest message on screen as read. Nothing ever called the old
    // QStringList version, which is why the other side only saw a message go read
    // when it was replied to - a reply marks the chat read as a side effect.
    Q_INVOKABLE void viewMessagesUpTo(int index) noexcept;
    Q_INVOKABLE void deleteMessage(qlonglong messageId, bool revoke = false) noexcept;

    Q_INVOKABLE void sendMessage(const QString &message, qlonglong replyToMessageId = 0) noexcept;
    Q_INVOKABLE void sendPhoto(const QString &filePath, const QString &caption = {}, qlonglong replyToMessageId = 0) noexcept;
    Q_INVOKABLE void editMessage(qlonglong messageId, const QString &text) noexcept;

    Q_INVOKABLE void fetchMoreBack() noexcept;

    Q_INVOKABLE int lastMessageIndex() const noexcept;

signals:
    void countChanged();
    void loadingChanged();

    void backFetchingChanged();

    void fetchedPosition(int numItems);

    // A single new message landed at the end - sent or received. Distinct from
    // countChanged, which also fires when a page of history is prepended.
    void messageAppended();

public slots:
    void refresh() noexcept;

private slots:
    void handleChatItem() noexcept;
    void handleResult(td::td_api::Object *object) noexcept;

    // Runs linkContentFile over everything loaded, on the main thread. registerFile is
    // idempotent, so re-linking an already canonical file costs a hash lookup.
    void linkLoadedContentFiles() noexcept;

private:
    void handleNewMessage(td::td_api::object_ptr<td::td_api::message> &&message) noexcept;
    void handleMessageContent(qlonglong chatId, qlonglong messageId, td::td_api::object_ptr<td::td_api::MessageContent> &&newContent) noexcept;
    void handleMessageEdited(qlonglong chatId, qlonglong messageId, int editDate, td::td_api::object_ptr<td::td_api::ReplyMarkup> &&replyMarkup) noexcept;
    void handleDeleteMessages(qlonglong chatId, std::vector<int64_t> &&messageIds, bool isPermanent, bool fromCache) noexcept;

    void loadMessages() noexcept;

    // Wraps any content in a sendMessage, so the reply plumbing lives in one place.
    void send(td::td_api::object_ptr<td::td_api::InputMessageContent> content, qlonglong replyToMessageId) noexcept;

    // Swaps any File the content built for itself for StorageManager's canonical one,
    // so the download reaches the object the delegate is bound to. Must run before
    // the message becomes visible to the view.
    void linkContentFile(Message *message) noexcept;

    // Reply previews. Both return an empty string when the message is not a reply,
    // which is what the delegate tests to decide whether to show a quote block.
    QString replyToSender(const Message *message) const noexcept;
    QString replyToText(const Message *message) const noexcept;

    void itemChanged(size_t index) noexcept;

    void insertMessages(std::vector<qlonglong> &&newIds, bool prepend);

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storage;

    std::shared_ptr<Chat> m_chat;

    bool m_loading{true};
    bool m_backFetching{true};

    std::vector<qlonglong> m_messages;
    std::unordered_map<qlonglong, std::unique_ptr<Message>> m_messageMap;
};
