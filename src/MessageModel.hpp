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
        // The same id as a string, for comparing against one and for handing back to a
        // model call. IdRole crosses into QML1 as a number, and this codebase does not
        // trust what happens to a qlonglong on that trip - see the note in Chat.hpp.
        // Read by every bubble now that reacting to one sends this id back; it is a
        // QString::number, which is not what a row read costs.
        IdStringRole,
        SenderRole,
        // The sender name with emoji replaced by <img> tags, for the bubble to display.
        // Separate from SenderRole because the plain one is carried into the reply
        // composer, where markup would show through.
        SenderHtmlRole,
        // The sender's own avatar, or null. Filled in for every message, including your
        // own: with bubbles off there is no side of the screen to say who spoke, so every
        // row carries one. Whether it is drawn is ShowsSenderRole's question, and the
        // bubble's binding short-circuits before reading this when the answer is no.
        SenderPhotoRole,
        // The colour Telegram gives this sender's name, picked from the sender id so the
        // same person keeps the same one everywhere. Filled in for every message, for the
        // same reason as the photo above.
        SenderColorRole,
        // The sender's rank in this group, beside the name: their custom title, or
        // "owner" / "admin" when they have none. Empty for an ordinary member - which is
        // nearly everyone - and for every message ShowsSenderRole is false on.
        SenderTitleRole,
        // Whether the avatar and the coloured name belong on this message *when it is
        // drawn as a bubble*: only where several people are talking, and only against
        // someone else's message - your own balloon is on the other side and already
        // yours. With bubbles off the bubble ignores this and shows them on everything,
        // which is the only thing left that says who spoke.
        ShowsSenderRole,
        // Whether this message starts a run - the message above it is from somebody else,
        // or is a service message, or sits under a different day header, or there is no
        // message above it. The flat layout hangs the avatar and the name off this, so a
        // burst of five messages from one person carries one of each instead of five.
        //
        // A property of the row's *neighbour*, not of the row, so it is computed on read
        // and the seam is refreshed on every structural change - see refreshRunAt.
        OpensRunRole,
        ChatIdRole,
        IsOutgoingRole,
        DateRole,
        EditDateRole,
        ContentRole,
        // The photos of an album, for the one row that draws it. Empty on every other row.
        AlbumRole,
        // The caption of an album, on that same row. Telegram puts it on whichever member
        // of the batch happens to carry it, so the row that draws the mosaic usually is
        // not the one holding the text - and the delegate cannot read it off its own
        // content. Walked here rather than in QML: reading a property off an element of
        // the AlbumRole list inline gives undefined in QML1, which is how the caption
        // went missing from the bubble while the model had it all along.
        AlbumCaptionRole,
        // Custom role
        ContentTypeRole,
        IsServiceRole,
        ServiceMessageRole,
        SectionRole,
        ReplyToSenderRole,
        ReplyToTextRole,
        // The message a reply points at, as a string - what the quote block hands to
        // indexOf to jump there. Empty when the message is not a reply.
        ReplyToMessageIdRole,
        // Delivery state of an outgoing message, for the tick on the bubble. Empty for
        // anything incoming.
        SendStateRole,
        // Reaction pills: a list of { emoji, icon, count, chosen }. Empty for the
        // messages nobody has reacted to, which is nearly all of them.
        ReactionsRole
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

    // Adds your reaction, or takes it away when it is already yours - what one tap on a
    // pill does. Which of the two goes out is decided here rather than passed in from
    // QML: the pill the user tapped and the message can be one update apart, and the
    // message is the one that is right. Nothing is drawn optimistically either; TDLib
    // answers with updateMessageInteractionInfo and that is what repaints the row.
    Q_INVOKABLE void toggleReaction(const QString &messageId, const QString &emoji) noexcept;

    // Whether the message carries any reaction this client can draw - what decides
    // whether the menu offers to list them. Answered off the loaded message rather than
    // passed down from the bubble, which would mean another argument on every one of the
    // seven delegates that raise the menu.
    Q_INVOKABLE bool hasReactions(const QString &messageId) const noexcept;

    // Who reacted, and with what. A request rather than a role: the names are not on the
    // message - it carries counts - and this is only ever wanted for the one message
    // somebody long-pressed. The answer comes back on messageReactionsReceived.
    Q_INVOKABLE void getMessageReactions(const QString &messageId) noexcept;

    // The person who sent a message, as a string for ChatManager::openProfile - a user id
    // is also the id of the private chat with them. Empty when the sender is a chat
    // rather than a person, which is what hides the menu entry and makes tapping the
    // avatar on a channel-signed post do nothing.
    //
    // Not a role: it is read when something is tapped, and a role would be read for every
    // row the list builds.
    Q_INVOKABLE QString senderUserId(const QString &messageId) const noexcept;

    // mentionUserIds and mentionNames are newline-joined and paired by index: the people
    // picked from the composer's autocomplete who have no username, whose names went into
    // the text as ordinary words. Each one that is still present in the message goes out
    // with an entity pointing at the id, which is what makes it a real mention.
    Q_INVOKABLE void sendMessage(const QString &message, const QString &replyToMessageId = {}, const QString &mentionUserIds = {},
                                 const QString &mentionNames = {}) noexcept;
    Q_INVOKABLE void sendPhoto(const QString &filePath, const QString &caption = {}, const QString &replyToMessageId = {}) noexcept;
    // Several photos as one album, up to the server's limit of ten. The paths arrive
    // newline-joined rather than as a QStringList: the picker holds its selection as one
    // string for the same reason - a JS array crossing the QML1 boundary is the class of
    // conversion that already cost this codebase an album caption.
    Q_INVOKABLE void sendPhotos(const QString &filePaths, const QString &caption = {},
                                const QString &replyToMessageId = {}) noexcept;
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

    // Row holding a message id, or -1 when it is not in the loaded slice - which is the
    // answer for a reply pointing further back than the loaded window, and for one
    // pointing into another chat.
    Q_INVOKABLE int indexOf(const QString &messageId) const noexcept;

signals:
    void countChanged();
    void loadingChanged();

    void fetchedPosition(int numItems);

    // A single new message landed at the end - sent or received. Distinct from
    // countChanged, which also fires when a page of history is prepended.
    void messageAppended();

    // The answer to getMessageReactions: a list of { name, emoji, icon }, in the order
    // TDLib gave them. Empty when the request failed - a big group only lets its admins
    // ask - which is what the dialog turns into a banner.
    void messageReactionsReceived(const QVariantList &senders);

public slots:
    void refresh() noexcept;

private slots:
    void handleResult(td::td_api::Object *object) noexcept;

    // The getChatHistory reply, handed over from the TDLib worker thread. Takes ownership
    // of the raw pointer; void* because a queued Q_ARG needs a registered metatype and
    // td_api::object_ptr is move-only, the same handover Client::dispatch uses.
    void handleHistoryResponse(void *responseObject, bool fetchPrevious) noexcept;

    // The getMessageAddedReactions reply, handed over from the TDLib worker thread on the
    // same void* terms as handleHistoryResponse - it resolves names through StorageManager,
    // which belongs to the GUI thread.
    void handleAddedReactions(void *responseObject) noexcept;

    // The getChatAdministrators reply, on the same terms - it fills m_admins and drops the
    // formatting cache, both of which the GUI thread reads.
    void handleChatAdministrators(void *responseObject) noexcept;

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
    // Reactions appearing, changing or going away - yours and everyone else's arrive the
    // same way. Only repaints the row: the counts live on the message, and this update
    // replaces that block whole.
    void handleMessageInteractionInfo(qlonglong chatId, qlonglong messageId,
                                      td::td_api::object_ptr<td::td_api::messageInteractionInfo> &&interactionInfo) noexcept;
    void handleDeleteMessages(qlonglong chatId, std::vector<int64_t> &&messageIds, bool isPermanent, bool fromCache) noexcept;

    void loadMessages() noexcept;

    // One request per chat, not one per sender: getChatAdministrators answers with the
    // whole list, which in any group a phone is reading is a handful of people. Skipped
    // outside a group, where no bubble shows a rank anyway.
    void requestAdministrators() noexcept;

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

    // Photos sent in one batch share a media_album_id and land in consecutive rows, since
    // they are consecutive ids. The first of the run draws the whole album and the rest
    // draw nothing, which is what ContentTypeRole reports as messageAlbum /
    // messageAlbumChild. Photos only: an album can also carry videos, and mixing two
    // content types into one mosaic buys nothing here.
    bool sameAlbum(int firstRow, int secondRow) const noexcept;
    int albumHead(int row) const noexcept;

    // Rows appearing or disappearing next to an album change which row is its head and
    // how many photos it holds, and neither is a value the row itself carries - so the
    // whole run has to be told to re-read. Called from every structural change.
    void refreshAlbumAt(int row) noexcept;

    // OpensRunRole for one row, computed from the row above it.
    bool opensRun(int row) const noexcept;

    // An insert or a removal changes whether the row at the seam opens a run, and only
    // that row. Appending never does - a message arriving at the end cannot change what
    // is above it - which is why the busy path pays nothing for this.
    void refreshRunAt(int row) noexcept;

    // Reply previews. Both return an empty string when the message is not a reply,
    // which is what the delegate tests to decide whether to show a quote block.
    QString replyToSender(const Message *message) const noexcept;
    QString replyToText(const Message *message) const noexcept;

    // "sending" / "failed" / "sent" / "read". Deliberately not part of FormattedRow: it
    // is the one value here that changes without the message changing, when the other
    // side reads the chat.
    QString sendState(const Message *message) const noexcept;

    // Reaction pills for one message. Not part of FormattedRow: the values are ints and
    // one already-resolved filename, so there is no date format and no storage lookup to
    // cache, and a message with no reactions - which is most of them - falls out on the
    // first line.
    QVariantList reactions(const Message *message) const noexcept;

    // The roles whose values cost a date format, a storage lookup or a content
    // preview. QML1 has no per-row role cache, so a bubble reading six properties
    // calls data() six times - and every one of those reads used to re-run
    // QDateTime::toString (with a tr() lookup for the format), getSenderName and
    // both reply previews. Same shape and same purpose as ChatModel::FormattedRow.
    struct FormattedRow
    {
        QString sender;
        QString senderHtml;
        QString senderColor;
        QString senderTitle;
        bool showsSender{false};
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

    // Sender id to the rank shown beside their name. Only the people who have one are in
    // here, so an ordinary member costs a failed lookup and nothing else.
    std::unordered_map<qlonglong, QString> m_admins;
};
