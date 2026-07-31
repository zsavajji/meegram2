#pragma once

#include "BasicGroup.hpp"
#include "Chat.hpp"
#include "ChatFolderModel.hpp"
#include "Client.hpp"
#include "File.hpp"
#include "Supergroup.hpp"
#include "SupergroupFullInfo.hpp"
#include "User.hpp"

#include <td/telegram/td_api.h>

#include <memory>
#include <unordered_map>

class StorageManager : public QObject
{
    Q_OBJECT
public:
    explicit StorageManager(std::shared_ptr<Client> client, QObject *parent = nullptr);

    [[nodiscard]] std::shared_ptr<Client> client() const noexcept;

    [[nodiscard]] std::vector<qlonglong> chatIds() const noexcept;

    [[nodiscard]] std::vector<std::shared_ptr<ChatFolderInfo>> chatFolders() const noexcept;

    [[nodiscard]] std::shared_ptr<BasicGroup> basicGroup(qlonglong groupId) const noexcept;
    [[nodiscard]] std::shared_ptr<Chat> chat(qlonglong chatId) const noexcept;
    [[nodiscard]] std::shared_ptr<File> file(int fileId) const noexcept;

    // Publishes a File as the one instance for its id, and returns whichever instance
    // is canonical - which may not be the one passed in. updateFile only ever mutates
    // the mapped object, so anything that builds its own File from an embedded
    // td_api::file has to adopt the result or it will never see the download finish.
    std::shared_ptr<File> registerFile(std::shared_ptr<File> file) noexcept;
    [[nodiscard]] std::shared_ptr<Supergroup> supergroup(qlonglong groupId) const noexcept;
    [[nodiscard]] std::shared_ptr<SupergroupFullInfo> supergroupFullInfo(qlonglong groupId) const noexcept;
    [[nodiscard]] std::shared_ptr<User> user(qlonglong userId) const noexcept;

signals:
    void chatFoldersUpdated();
    void basicGroupUpdated(qlonglong groupId);
    // Any change at all to a chat object, including the chat merely arriving in the
    // store. Fired from eleven different updates, so it means "look again", not
    // "something happened" - see chatLastMessageChanged before wiring anything that
    // has a side effect the user can see.
    void chatUpdated(qlonglong chatId);

    // The chat's last message actually changed. The narrow signal behind chatUpdated,
    // for the one case where acting on a plain chatUpdated is wrong: updateNewChat
    // carries a fully populated last_message_, so a chat merely being delivered - which
    // is what scrolling the list does, in batches - is indistinguishable from a message
    // arriving in it. Notifications went out for chats the user had only just scrolled
    // into view before this existed.
    void chatLastMessageChanged(qlonglong chatId);

    void chatPositionUpdated(qlonglong chatId);
    void supergroupUpdated(qlonglong groupId);
    void userUpdated(qlonglong userId);

    void chatOnlineMemberCountUpdated(qlonglong chatId, int onlineMemberCount);

    // Somebody started or stopped doing something in a chat - typing, recording,
    // uploading. actionType is the td_api id of the action, or 0 for "stopped".
    void chatActionUpdated(qlonglong chatId, qlonglong senderId, int actionType);

public slots:
    qlonglong myId() const noexcept;

    QVariant getOption(const QString &name) const noexcept;

private slots:
    void handleResult(td::td_api::Object *object);

private:
    void registerChatPhoto(const std::shared_ptr<Chat> &chat) noexcept;

    QVariantMap m_options;

    // myId() is reached once per chat row via getChatTitle()->isMeChat(), and each
    // call built a QString from a literal to do a map lookup. Reset whenever any
    // option changes; options are pushed rarely.
    mutable qlonglong m_myId{0};

    std::shared_ptr<Client> m_client;

    std::vector<std::shared_ptr<ChatFolderInfo>> m_chatFolders;

    std::unordered_map<qlonglong, std::shared_ptr<BasicGroup>> m_basicGroup;
    std::unordered_map<qlonglong, std::shared_ptr<Chat>> m_chats;
    std::unordered_map<int, std::shared_ptr<File>> m_files;
    std::unordered_map<qlonglong, std::shared_ptr<Supergroup>> m_supergroup;
    std::unordered_map<qlonglong, std::shared_ptr<SupergroupFullInfo>> m_supergroupFullInfo;
    std::unordered_map<qlonglong, std::shared_ptr<User>> m_users;
};
