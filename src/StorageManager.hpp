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
#include <unordered_set>

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

    // The one field of userFullInfo anything here shows, so it is kept as the string it
    // is displayed as rather than behind a class of its own. Empty until loadUserFullInfo
    // has been through.
    [[nodiscard]] QString userBio(qlonglong userId) const noexcept;

    // Fetches the full info a user's bio lives on. The answer is taken from the reply
    // rather than from updateUserFullInfo alone: TDLib only pushes that update when the
    // full info actually changed, so a second request for the same user - which is what
    // reopening a profile is - would otherwise be answered by nothing at all.
    //
    // Lives here rather than on the caller because the reply arrives on the TDLib worker
    // thread, and this object outlives every page that asks; a ChatInfoFormatter is
    // destroyed the moment another chat is opened.
    void loadUserFullInfo(qlonglong userId) noexcept;

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
    void userFullInfoUpdated(qlonglong userId);

    void chatOnlineMemberCountUpdated(qlonglong chatId, int onlineMemberCount);

    // Somebody started or stopped doing something in a chat - typing, recording,
    // uploading. actionType is the td_api id of the action, or 0 for "stopped".
    void chatActionUpdated(qlonglong chatId, qlonglong senderId, int actionType);

public slots:
    qlonglong myId() const noexcept;

    QVariant getOption(const QString &name) const noexcept;

    // Puts a chat in the store that TDLib has already announced to somebody else. Does
    // nothing for a chat already held, and nothing for one already being fetched.
    //
    // updateNewChat is emitted once per TDLib process - send_update_new_chat latches
    // d->is_update_new_chat_sent - and meegramd's TDLib outlives every UI that attaches
    // to it. So for a resident daemon, "the update that would have told us" has already
    // been sent to a run that is gone, and asking is the only way left. getChat answers
    // in ~10 ms with ~14 KB, and the reply *is* the chat.
    //
    // A slot, because ChatModel calls it from the TDLib worker thread with a queued
    // invocation: the maps below are only ever written on the GUI thread.
    void fetchChat(qlonglong chatId) noexcept;

private slots:
    void handleResult(td::td_api::Object *object);

    // Queued from the getChat callback in fetchChat, for the same reason setUserBio is.
    void clearChatFetch(qlonglong chatId) noexcept;

    // Queued from the getUserFullInfo callback, so the map is only ever written on this
    // thread. See loadUserFullInfo.
    void setUserBio(qlonglong userId, const QString &bio) noexcept;

private:
    void registerChatPhoto(const std::shared_ptr<Chat> &chat) noexcept;
    void registerUserPhoto(const std::shared_ptr<User> &user) noexcept;

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
    std::unordered_map<qlonglong, QString> m_userBios;

    // In flight in fetchChat. A chat list asks for the same ids on every page it loads -
    // getChats returns the whole slice each time, not just the new tail - so without this
    // a scroll to the bottom re-requests every row above it.
    std::unordered_set<qlonglong> m_fetchingChats;
};
