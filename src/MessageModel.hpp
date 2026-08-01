#pragma once

#include <td/telegram/td_api.h>

#include <QAbstractListModel>
#include <QTimer>

#include <atomic>
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

public:
    explicit MessageModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);
    ~MessageModel() override;

    enum Role {
        IdRole = Qt::UserRole + 1,
        SenderRole,
        // The sender name with emoji replaced by <img> tags, for the bubble to display.
        // Separate from SenderRole because the plain one is carried into the reply
        // composer, where markup would show through.
        SenderHtmlRole,
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
        ReplyToTextRole,
        // Delivery state of an outgoing message, for the tick on the bubble. Empty for
        // anything incoming.
        SendStateRole
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    bool canFetchMore(const QModelIndex &parent = QModelIndex()) const override;
    void fetchMore(const QModelIndex &parent = QModelIndex()) override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const noexcept;

    int count() const noexcept;
    bool loading() const noexcept;

    Q_INVOKABLE void getChatHistory(const QString &fromMessageId, int offset, int limit, bool fetchPrevious = false) noexcept;
    // Reports the newest message on screen as read. Nothing ever called the old
    // QStringList version, which is why the other side only saw a message go read
    // when it was replied to - a reply marks the chat read as a side effect.
    Q_INVOKABLE void viewMessagesUpTo(int index) noexcept;
    // Marks one message's content as opened - what makes a voice note count as listened
    // on the other clients. viewMessages only moves the read pointer, it does not do this.
    Q_INVOKABLE void openMessageContent(const QString &messageId) noexcept;
    Q_INVOKABLE void deleteMessage(const QString &messageId, bool revoke = false) noexcept;

    Q_INVOKABLE void sendMessage(const QString &message, const QString &replyToMessageId = {}) noexcept;
    Q_INVOKABLE void sendPhoto(const QString &filePath, const QString &caption = {}, const QString &replyToMessageId = {}) noexcept;
    // Any file, as a document - including an image, which is what "send as file" means
    // to Telegram and is why this does not sniff the type and reroute to sendPhoto.
    Q_INVOKABLE void sendDocument(const QString &filePath, const QString &caption = {}, const QString &replyToMessageId = {}) noexcept;
    // The duration is passed in rather than read back off the file: VoiceNote counted the
    // samples as it captured them, and re-deriving it would mean decoding what was just
    // encoded. TDLib shows it to the recipient before the note finishes downloading.
    Q_INVOKABLE void sendVoiceNote(const QString &filePath, int duration, const QString &caption = {},
                                   const QString &replyToMessageId = {}) noexcept;
    Q_INVOKABLE void editMessage(const QString &messageId, const QString &text) noexcept;

    Q_INVOKABLE void fetchMoreBack() noexcept;

    // Row of the last read message, or count() when it is not in the loaded slice.
    Q_INVOKABLE int lastMessageIndex() const noexcept;

signals:
    void countChanged();
    void loadingChanged();

    void fetchedPosition(int numItems);

    // A single new message landed at the end - sent or received. Distinct from
    // countChanged, which also fires when a page of history is prepended.
    void messageAppended();

public slots:
    void refresh() noexcept;

private slots:
    void handleResult(td::td_api::Object *object) noexcept;

    // The getChatHistory reply, handed over from the TDLib worker thread. Takes ownership
    // of the raw pointer; void* because a queued Q_ARG needs a registered metatype and
    // td_api::object_ptr is move-only, the same handover Client::disposeObject uses.
    void handleHistoryResponse(void *responseObject, bool fetchPrevious) noexcept;

    // Runs linkContentFile over everything loaded, on the main thread. registerFile is
    // idempotent, so re-linking an already canonical file costs a hash lookup.
    void linkLoadedContentFiles() noexcept;

    // Re-asks for history after TDLib answered with nothing. Bounded, so a genuinely
    // empty chat stops asking.
    void reloadHistory() noexcept;

private:
    void handleNewMessage(td::td_api::object_ptr<td::td_api::message> &&message) noexcept;
    // The sent-message swap: the temporary id updateNewMessage delivered is replaced by
    // the real one. Both the succeeded and the failed update carry it, and both retire
    // the same pending row.
    void handleMessageSendCompleted(td::td_api::object_ptr<td::td_api::message> &&message, qlonglong oldMessageId) noexcept;
    // Only repaints the ticks: the value itself lives on the Chat, which StorageManager
    // has already updated by the time this runs.
    void handleChatReadOutbox(qlonglong chatId, qlonglong lastReadOutboxMessageId) noexcept;
    void handleMessageContent(qlonglong chatId, qlonglong messageId, td::td_api::object_ptr<td::td_api::MessageContent> &&newContent) noexcept;
    void handleMessageEdited(qlonglong chatId, qlonglong messageId, int editDate, td::td_api::object_ptr<td::td_api::ReplyMarkup> &&replyMarkup) noexcept;
    void handleDeleteMessages(qlonglong chatId, std::vector<int64_t> &&messageIds, bool isPermanent, bool fromCache) noexcept;

    void loadMessages() noexcept;

    // Wraps any content in a sendMessage, so the reply plumbing lives in one place.
    void send(td::td_api::object_ptr<td::td_api::InputMessageContent> content, qlonglong replyToMessageId) noexcept;

    // The real request. getChatHistory is only the QML entry point, which exists to
    // take the id as a string - see toId() in Common.hpp. Internal callers already
    // hold a qlonglong and have no reason to round-trip it through one.
    void requestHistory(qlonglong fromMessageId, int offset, int limit, bool fetchPrevious = false) noexcept;

    // Swaps any File the content built for itself for StorageManager's canonical one,
    // so the download reaches the object the delegate is bound to. Must run before
    // the message becomes visible to the view.
    void linkContentFile(Message *message) noexcept;

    // Reply previews. Both return an empty string when the message is not a reply,
    // which is what the delegate tests to decide whether to show a quote block.
    QString replyToSender(const Message *message) const noexcept;
    QString replyToText(const Message *message) const noexcept;

    // "sending" / "failed" / "sent" / "read". Deliberately not part of FormattedRow: it
    // is the one value here that changes without the message changing, when the other
    // side reads the chat.
    QString sendState(const Message *message) const noexcept;

    // The roles whose values cost a date format, a storage lookup or a content
    // preview. QML1 has no per-row role cache, so a bubble reading six properties
    // calls data() six times - and every one of those reads used to re-run
    // QDateTime::toString (with a tr() lookup for the format), getSenderName and
    // both reply previews. Same shape and same purpose as ChatModel::FormattedRow.
    struct FormattedRow
    {
        QString sender;
        QString senderHtml;
        QString date;
        QString section;
        QString replyToSender;
        QString replyToText;
        bool valid{false};
    };

    const FormattedRow &formattedRow(qlonglong messageId, const Message *message) const noexcept;

    // Today / Yesterday / the full date, for the list's section headers.
    QString sectionFor(const Message *message) const noexcept;

    void itemChanged(size_t index) noexcept;

    void insertMessages(std::vector<qlonglong> &&newIds, bool prepend);

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storage;

    // Cleared by the destructor so the getChatHistory callback, which runs on the TDLib
    // worker thread and captured a raw this, knows the model is gone. Leaving a chat
    // while a history request was in flight was a use-after-free. Same guard and same
    // reason as ChatModel's.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};

    std::shared_ptr<Chat> m_chat;

    bool m_loading{true};
    bool m_backFetching{true};

    // TDLib can answer getChatHistory with nothing while its own fetch is still in
    // flight, so an empty reply is retried rather than believed.
    QTimer m_historyRetryTimer;
    int m_historyRetries{0};

    static constexpr int MaxHistoryRetries = 3;

    std::vector<qlonglong> m_messages;
    std::unordered_map<qlonglong, std::unique_ptr<Message>> m_messageMap;

    mutable std::unordered_map<qlonglong, FormattedRow> m_formatted;
};
