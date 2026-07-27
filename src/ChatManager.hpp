#pragma once

#include "ChatFolderModel.hpp"
#include "ChatModel.hpp"
#include "MessageModel.hpp"

#include <QObject>
#include <QTimer>

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

public:
    explicit ChatInfoFormatter(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage);

    QString title() const noexcept;
    QString status() const noexcept;

signals:
    void statusChanged();

private slots:
    void handleBasicGroupUpdate(qlonglong groupId) noexcept;
    void handleSupergroupUpdate(qlonglong groupId) noexcept;
    void handleUserUpdate(qlonglong userId) noexcept;
    void handleChatOnlineMemberCount(qlonglong chatId, int onlineMemberCount) noexcept;

    void handleChatAction(qlonglong chatId, qlonglong senderId, int actionType) noexcept;
    void clearChatAction() noexcept;

private:
    void initializeMembers() noexcept;
    void updateStatus() noexcept;

    QString formatStatus(int memberCount, const char *memberKey, const char *onlineKey) const noexcept;
    int getMemberCountWithFallback() const noexcept;
    bool isServiceNotification() const noexcept;
    QString formatUserStatus() const noexcept;
    QString formatOfflineStatus() const noexcept;

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
};

class ChatManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *folderModel READ folderModel NOTIFY folderModelChanged)

    Q_PROPERTY(QObject *mainModel READ mainModel NOTIFY mainModelChanged)
    Q_PROPERTY(QObject *archivedModel READ archivedModel NOTIFY archivedModelChanged)
    Q_PROPERTY(QList<QObject *> folderModels READ folderModels NOTIFY folderModelsChanged)

    Q_PROPERTY(Chat *selectedChat READ selectedChat NOTIFY selectedChatChanged)

    Q_PROPERTY(QObject *chatInfo READ chatInfoFormatter NOTIFY selectedChatChanged)
    Q_PROPERTY(QObject *messageModel READ messageModel NOTIFY selectedChatChanged)

public:
    explicit ChatManager(std::shared_ptr<StorageManager> storageManager, std::shared_ptr<Locale> locale);

    QObject *folderModel() const noexcept;

    QObject *mainModel() const noexcept;
    QObject *archivedModel() const noexcept;
    QList<QObject *> folderModels() const noexcept;

    Chat *selectedChat() const noexcept;

    QObject *chatInfoFormatter() const noexcept;
    QObject *messageModel() const noexcept;

    // False when the chat is not in StorageManager, in which case nothing was selected
    // and the caller must not push a page that would bind to nothing. A fetch is started
    // for it, so chatAvailable() follows either way.
    Q_INVOKABLE bool openChat(const QString &chatId) noexcept;
    Q_INVOKABLE void closeChat(const QString &chatId) noexcept;

signals:
    void selectedChatChanged();

    // A chat that openChat() refused has finished being fetched. ok says whether it can
    // be opened now; the caller retries openChat() or reports the failure.
    // chatId is a decimal string: main.qml feeds it straight back into openChat(), and
    // every Q_INVOKABLE on this class already takes ids that way. No C++ listener.
    void chatAvailable(const QString &chatId, bool ok);

    // The chat the user is looking at, or 0 when none. Drives notification
    // suppression; separate from selectedChatChanged because that one carries no id
    // and does not fire on close.
    void activeChatChanged(qlonglong chatId);

    void folderModelChanged();

    void mainModelChanged();
    void archivedModelChanged();
    void folderModelsChanged();

private slots:
    void onChatFoldersUpdated() noexcept;
    void handleChatFetched(qlonglong chatId, bool ok) noexcept;

private:
    void updateFolderModels() noexcept;
    void fetchChat(qlonglong chatId) noexcept;

    // The one chat currently being fetched by fetchChat(), so a fetch that succeeds
    // without making the chat openable cannot bounce between here and openChat().
    qlonglong m_fetchingChatId{0};

    std::shared_ptr<Client> m_client;
    std::shared_ptr<Locale> m_locale;
    std::shared_ptr<StorageManager> m_storage;

    std::unique_ptr<ChatModel> m_mainModel;
    std::unique_ptr<ChatModel> m_archivedModel;
    std::vector<std::unique_ptr<ChatModel>> m_folderModels;

    std::unique_ptr<ChatFolderModel> m_folderModel;

    std::shared_ptr<Chat> m_selectedChat;

    std::unique_ptr<ChatInfoFormatter> m_infoFormatter;
    std::unique_ptr<MessageModel> m_messageModel;
};
