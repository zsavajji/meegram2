#pragma once

#include "ChatFolderModel.hpp"
#include "ChatModel.hpp"
#include "MessageModel.hpp"
#include "SearchModel.hpp"

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <memory>
#include <vector>

class Client;
class StorageManager;
class Chat;
class BasicGroup;
class StorageManager;
class Supergroup;
class User;

class ChatInfoFormatter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    // What the profile page shows under the photo. All three are empty when they do not
    // apply - a group has no phone number, a user who hides theirs has none either - so
    // the page hides a row by testing its string.
    Q_PROPERTY(QString username READ username NOTIFY profileChanged)
    Q_PROPERTY(QString bio READ bio NOTIFY profileChanged)
    Q_PROPERTY(QString phoneNumber READ phoneNumber NOTIFY profileChanged)

    // False in a channel you are only subscribed to. Channels are broadcast-only, so the
    // page hides its whole composer rather than offering a send TDLib will refuse. True
    // for every other chat type, including supergroups.
    Q_PROPERTY(bool canSendMessages READ canSendMessages NOTIFY canSendMessagesChanged)

    // The group's members as { name, tag, status, photo }, most recently seen first.
    // Empty until loadMembers is called and until its answer lands, and empty for good in
    // anything that is not a group. A plain list rather than a model: the page asks once
    // on the way in and never follows the members after that, so there is nothing for a
    // QAbstractListModel's change signals to carry.
    Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)

public:
    explicit ChatInfoFormatter(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);
    ~ChatInfoFormatter() override;

    QString title() const noexcept;
    QString status() const noexcept;

    QString username() const noexcept;
    QString bio() const noexcept;
    QString phoneNumber() const noexcept;

    bool canSendMessages() const noexcept;

    QVariantList members() const noexcept;

    // Asks TDLib for the bio, which arrives as an update rather than as an answer here.
    // Called by the profile page on the way in; everything else is already in store.
    Q_INVOKABLE void loadProfile() noexcept;

    // The member list, one snapshot per visit to the profile page. Called from the same
    // place as loadProfile and answered on membersChanged.
    Q_INVOKABLE void loadMembers() noexcept;

signals:
    void statusChanged();
    void profileChanged();
    void canSendMessagesChanged();
    void membersChanged();

private slots:
    // The getSupergroupMembers / getBasicGroupFullInfo reply, handed over from the TDLib
    // worker thread as a void* - the same handover MessageModel::handleHistoryResponse
    // documents, and for the same reason: it resolves users through StorageManager, which
    // belongs to the GUI thread.
    void handleChatMembers(void *responseObject) noexcept;

    void handleBasicGroupUpdate(qlonglong groupId) noexcept;
    void handleSupergroupUpdate(qlonglong groupId) noexcept;
    void handleUserUpdate(qlonglong userId) noexcept;
    void handleUserFullInfo(qlonglong userId) noexcept;
    void handleChatOnlineMemberCount(qlonglong chatId, int onlineMemberCount) noexcept;

    void handleChatAction(qlonglong chatId, qlonglong senderId, int actionType) noexcept;
    void clearChatAction() noexcept;

private:
    void initializeMembers() noexcept;
    void updateStatus() noexcept;

    QString formatStatus(int memberCount, const char *memberKey, const char *onlineKey) const noexcept;
    int getMemberCountWithFallback() const noexcept;
    bool isServiceNotification() const noexcept;
    // Take the user rather than reading m_user, so the member list can format the same
    // "last seen" string the header shows for whoever the chat is with.
    QString formatUserStatus(const std::shared_ptr<User> &user) const noexcept;
    QString formatOfflineStatus(const std::shared_ptr<User> &user) const noexcept;

    // One member row, or an empty map when the member is a chat rather than a person or
    // is not in store yet.
    QVariantMap formatMember(const td::td_api::chatMember &member) const noexcept;

    int m_onlineMemberCount{};

    QString m_title, m_status;

    // What somebody is currently doing in this chat, shown in place of the status.
    // TDLib repeats the action every few seconds while it continues and sends a cancel
    // when it stops - but a client that missed the cancel would show "typing" forever,
    // so it also expires on its own.
    QString m_action;
    QTimer m_actionTimer;

    std::shared_ptr<Chat> m_chat;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storageManager;

    std::shared_ptr<User> m_user;
    std::shared_ptr<BasicGroup> m_basicGroup;
    std::shared_ptr<Supergroup> m_supergroup;

    QVariantList m_members;

    // Cleared by the destructor so the member-list callback, which runs on the TDLib
    // worker thread and captured a raw this, knows the formatter is gone - opening
    // another profile destroys this one while the request is still out. Same guard and
    // same reason as MessageModel's.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
};

// Everything one pushed page binds to, owned for exactly as long as that page exists.
//
// Pages used to bind to ChatManager itself: one "selected chat" slot and one "current
// profile" slot, both rewritten on every open. Every page alive shared them, so opening
// anything from a page rewrote that page and then pushed the new one on top of it - a
// profile turned into the member you tapped before being pushed again as a second copy,
// and a chat opened from a profile rebound the ChatPage underneath to the new
// conversation. One bug in two places, and this is the single fix for both: a page is
// handed its own context at push time and reads no shared state at all.
//
// Everything a page needs is here rather than passed as three separate initial properties,
// so there is no way to hand a page a chat and somebody else's formatter.
class ChatContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Chat *chat READ chat CONSTANT)
    // The same id as a string, for handing back to a manager call. See the note on ids in
    // Chat.hpp for why it does not cross into QML1 as a number.
    Q_PROPERTY(QString chatId READ chatId CONSTANT)
    Q_PROPERTY(QObject *info READ info CONSTANT)

    // Null on a context pushed for a profile page: it shows no messages, and building a
    // model for it would start a history fetch nobody reads.
    Q_PROPERTY(QObject *messageModel READ messageModel CONSTANT)

    // Handed back to popContext() when the page is destroyed. An int rather than the
    // context itself: the page holds it in a `property variant`, and a variant that has
    // been round-tripped back into a C++ QObject* parameter is exactly the QML1
    // conversion this codebase has been caught by before. A number crosses both ways with
    // nothing to get wrong.
    Q_PROPERTY(int token READ token CONSTANT)

public:
    ChatContext(int token, std::shared_ptr<Chat> chat, std::unique_ptr<ChatInfoFormatter> info,
                std::unique_ptr<MessageModel> messageModel, QObject *parent = nullptr);

    int token() const noexcept;
    Chat *chat() const noexcept;
    QString chatId() const noexcept;
    QObject *info() const noexcept;
    QObject *messageModel() const noexcept;

    // True when this context is a chat being read rather than a profile being looked at -
    // which is to say, when it has a message model. What decides whether it takes part in
    // openChat/closeChat and in notification suppression, the two things that are about
    // reading a chat rather than about showing one.
    bool isReading() const noexcept;

private:
    int m_token{0};

    // By shared_ptr and not by raw pointer or id: the object the page is bound to cannot
    // go away underneath it while its page is alive, whatever StorageManager does with
    // its own copy.
    std::shared_ptr<Chat> m_chat;

    // Owned outright, and one per context. Sharing a formatter is what let two profile
    // pages fight over one member list, and sharing a model is what left a popped
    // ChatPage's ListView reading the next chat's messages.
    std::unique_ptr<ChatInfoFormatter> m_info;
    std::unique_ptr<MessageModel> m_messageModel;
};

class ChatManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *folderModel READ folderModel CONSTANT)

    Q_PROPERTY(QObject *mainModel READ mainModel CONSTANT)
    Q_PROPERTY(QObject *archivedModel READ archivedModel CONSTANT)
    Q_PROPERTY(QList<QObject *> folderModels READ folderModels NOTIFY folderModelsChanged)

    // Starting a conversation with somebody who is not in the chat list yet. One model for
    // the whole session rather than one per page: the page is pushed and popped, and its
    // results are worth nothing once it is gone.
    Q_PROPERTY(QObject *searchModel READ searchModel CONSTANT)

    // The chat the user is reading - the topmost context that has a message model - as a
    // string, or empty when none. See the note on ids in Chat.hpp. Not a handle to
    // anything: a page binds to the context it was handed, and this exists for the one
    // question a page legitimately asks about somebody else's, which is "am I about to
    // push a duplicate of what is already underneath me".
    Q_PROPERTY(QString activeChatId READ activeChatId NOTIFY activeChatChanged)

public:
    explicit ChatManager(std::shared_ptr<StorageManager> storageManager, std::shared_ptr<Locale> locale);

    QObject *folderModel() const noexcept;

    QObject *mainModel() const noexcept;
    QObject *archivedModel() const noexcept;
    QList<QObject *> folderModels() const noexcept;

    QObject *searchModel() const noexcept;

    QString activeChatId() const noexcept;

    // Opens a profile for a chat id or an @username - a tapped mention carries one or the
    // other - without touching what is being read. Answers on profileReady: at once when
    // the chat is already known, after a round trip when it has to be resolved or created.
    Q_INVOKABLE void openProfile(const QString &target) noexcept;

    // A context for a page about to be pushed, or null when the chat is not in store - in
    // which case nothing was pushed, a fetch was started for it, and the caller must not
    // push a page that would bind to nothing. chatAvailable() follows either way.
    //
    // pushChat is for a ChatPage: it builds a message model and makes this the chat being
    // read, which is what TDLib and meegramd are told and what suppresses its
    // notifications. pushProfile is for a ProfilePage, which shows a chat without reading
    // it and leaves whatever is being read alone.
    //
    // The caller owns the pairing: every context handed out here must come back through
    // popContext() when its page is destroyed.
    Q_INVOKABLE QObject *pushChat(const QString &chatId) noexcept;
    Q_INVOKABLE QObject *pushProfile(const QString &chatId) noexcept;

    // Retires the context a page was given, called from that page's destruction with the
    // token it was handed. By token rather than by depth: PageStack is not the only thing
    // that destroys pages - clear() takes the whole stack at once, and a pop is deferred to
    // the end of the event loop - so retiring the top would take a context that is still on
    // screen. An unknown token is ignored, which is what makes a page destroyed twice
    // harmless.
    //
    // Whatever is left underneath becomes the chat being read again, so popping one of two
    // stacked ChatPages reopens the one below rather than leaving nothing open.
    Q_INVOKABLE void popContext(int token) noexcept;

    // Mention autocomplete: members of the chat being read whose name or username matches
    // what is being typed. Answers on mentionsFound, with an empty list for anything that
    // is not a group - so the composer does not have to know what kind of chat it is in.
    Q_INVOKABLE void searchMentions(const QString &query) noexcept;

    // Creates a group with the given members and reports it on chatAvailable, which is what
    // opens it - the same path a chat fetched by id takes. A basic group, as the official
    // clients make: Telegram upgrades it to a supergroup on its own when it outgrows one.
    Q_INVOKABLE void createGroup(const QString &title, const QStringList &userIds) noexcept;

    // Installed on the application, to close and reopen the chat being read as the window
    // loses and regains focus. Public because that is where QObject declares it.
    bool eventFilter(QObject *object, QEvent *event) noexcept override;

signals:
    // openProfile() has finished, successfully or not. The page is pushed from here
    // rather than by the caller, which cannot know whether the chat had to be fetched.
    //
    // chatId is the resolved chat as a decimal string - a username had to be looked up to
    // get it - and is what the caller feeds to pushProfile(). Empty on failure.
    //
    // reason is empty on success and says what went wrong otherwise - TDLib's own error,
    // or the lookup that followed it. It is shown in the banner because this device has
    // nowhere else to say it: qWarning goes to a stderr nothing collects, and the daemon
    // socket refuses any peer that is not the app itself, so a failure that is not
    // reported here cannot be investigated at all.
    void profileReady(bool ok, const QString &chatId, const QString &reason);

    // The answer to searchMentions(), oldest request wins nothing - a later reply simply
    // replaces the list. Lists paired by index rather than one of objects: QStringList is
    // the one list type that crosses into QML1 as a plain array, and a list of objects is
    // exactly the conversion that has bitten this codebase before.
    //
    // A username is empty for a member who has none. They are still offered: picking one
    // puts their name in as ordinary text and the message carries an entity pointing at
    // the id, which is how the official clients mention somebody without a username.
    void mentionsFound(const QStringList &usernames, const QStringList &names, const QStringList &userIds);

    // A chat that pushChat() refused has finished being fetched. ok says whether it can
    // be opened now; the caller retries the open or reports the failure.
    // chatId is a decimal string: main.qml feeds it straight back into its own openChat(),
    // and every Q_INVOKABLE on this class already takes ids that way. No C++ listener.
    void chatAvailable(const QString &chatId, bool ok);

    // The chat being read, or 0 when none - the top of the context stack changing, whether
    // by a push, a pop, or the last chat page going away. Drives notification suppression,
    // and is the notify for activeChatId.
    void activeChatChanged(qlonglong chatId);

    void folderModelsChanged();

private slots:
    void onChatFoldersUpdated() noexcept;
    void handleChatFetched(qlonglong chatId, bool ok) noexcept;

    // The two halves of openProfile() and searchMentions() that must run on the main
    // thread. handleChatMembers takes ownership of the raw pointer; void* because a
    // queued Q_ARG needs a registered metatype and td_api::object_ptr is move-only -
    // the same handover MessageModel::handleHistoryResponse makes, and for the same
    // reason: StorageManager must not be read from the TDLib worker thread.
    void handleProfileFetched(qlonglong chatId, const QString &reason) noexcept;
    void handleChatMembers(void *responseObject) noexcept;

private:
    void updateFolderModels() noexcept;
    void fetchChat(qlonglong chatId) noexcept;

    // The two pushes above, which differ only in whether the context reads its chat.
    ChatContext *pushContext(const QString &rawChatId, bool reading) noexcept;

    // The topmost context being read, or null when nothing on the stack is a ChatPage.
    // Scanned from the top rather than tracked in a member: a profile pushed over a chat
    // leaves the chat underneath still the one being read, and one rule - "the highest
    // reading context wins" - answers that without a second thing to keep in step.
    //
    // Answers "which context", not "which chat" - activeChatId reads m_openChatId, which
    // is the same answer from the one place that is always current. Callers here want the
    // object: the chat to search members in, and the chat to reopen after a pop.
    ChatContext *activeContext() const noexcept;

    // Moves the single open chat, telling TDLib and - through the relayed request -
    // meegramd. Exactly one at a time, which is what both of them assume: TDLib counts
    // opens against closes per dialog, and the daemon keeps one open-chat id. So the
    // previous one is closed before the new one opens, and pushing the same chat twice
    // changes nothing rather than opening it twice.
    void setOpenChat(qlonglong chatId) noexcept;

    // Bottom to top, one per live page that was handed a context. Ordinarily one to three
    // deep - a chat, a profile opened from it, a chat opened from that - so a vector
    // scanned by token is the whole lookup. Contexts are retired by popContext(), and any
    // left at shutdown are destroyed with this.
    std::vector<std::unique_ptr<ChatContext>> m_contextStack;

    // Handed out by pushContext() and never reused, so a token that outlives its context -
    // a page destroyed twice, or one destroyed after the stack was cleared - matches
    // nothing rather than matching whatever took its place.
    int m_nextToken{0};

    // What TDLib has been told is open, and the value the minimise/restore pair closes and
    // reopens. Zero when nothing is. Kept rather than re-derived so a close is always sent
    // for the id the matching open used, even if the stack changed underneath.
    qlonglong m_openChatId{0};

    // The one chat currently being fetched by fetchChat(), so a fetch that succeeds
    // without making the chat openable cannot bounce between here and pushChat().
    qlonglong m_fetchingChatId{0};

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storage;

    std::unique_ptr<ChatModel> m_mainModel;
    std::unique_ptr<ChatModel> m_archivedModel;
    std::vector<std::unique_ptr<ChatModel>> m_folderModels;

    std::unique_ptr<ChatFolderModel> m_folderModel;
    std::unique_ptr<SearchModel> m_searchModel;
};
