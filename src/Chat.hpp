#pragma once

#include "ChatPosition.hpp"
#include "File.hpp"
#include "Message.hpp"

#include <td/telegram/td_api.h>

#include <QObject>

#include <memory>

class Chat : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qlonglong id READ id CONSTANT)
    Q_PROPERTY(Type type READ type CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY chatChanged)
    Q_PROPERTY(File *photo READ photo NOTIFY chatChanged)
    Q_PROPERTY(Message *lastMessage READ lastMessage NOTIFY chatChanged)
    Q_PROPERTY(bool isMarkedAsUnread READ isMarkedAsUnread NOTIFY chatChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY chatChanged)
    Q_PROPERTY(int unreadMentionCount READ unreadMentionCount NOTIFY chatChanged)

    Q_PROPERTY(int muteFor READ muteFor NOTIFY chatChanged)
    Q_PROPERTY(bool isMuted READ isMuted NOTIFY chatChanged)

    // lastReadInboxMessageId, lastReadOutboxMessageId and typeId are deliberately not
    // Q_PROPERTY: they are qlonglong and read only from C++. A qlonglong reaching QML1
    // has to land in a `property variant` or it comes back corrupted, so the ones that
    // do cross are kept to id alone. Add the getter to a caller, not a property here.

    Q_ENUMS(Type)
public:
    explicit Chat(td::td_api::object_ptr<td::td_api::chat> chat, QObject *parent = nullptr);

    enum Type { None, Private, Secret, BasicGroup, Supergroup, Channel };

    qlonglong id() const noexcept;
    Type type() const noexcept;
    QString title() const noexcept;
    File *photo() const noexcept;

    // For StorageManager, so the chat's photo and its file map hold one object.
    const std::shared_ptr<File> &photoFile() const noexcept;
    void adoptPhotoFile(std::shared_ptr<File> file) noexcept;
    Message *lastMessage() const noexcept;
    bool isMarkedAsUnread() const noexcept;
    int unreadCount() const noexcept;
    qlonglong lastReadInboxMessageId() const noexcept;
    qlonglong lastReadOutboxMessageId() const noexcept;
    int unreadMentionCount() const noexcept;

    int muteFor() const noexcept;
    bool isMuted() const noexcept;

    qlonglong typeId() const noexcept;

    std::vector<std::unique_ptr<ChatPosition>> &positions() noexcept;

    void setTitle(std::string_view title) noexcept;
    void setPhoto(td::td_api::object_ptr<td::td_api::chatPhotoInfo> photo) noexcept;
    void setLastMessage(td::td_api::object_ptr<td::td_api::message> lastMessage) noexcept;
    void setPositions(std::vector<td::td_api::object_ptr<td::td_api::chatPosition>> positions) noexcept;
    void setIsMarkedAsUnread(bool isMarkedAsUnread) noexcept;
    void setUnreadCount(int unreadCount) noexcept;
    void setLastReadInboxMessageId(qlonglong lastReadInboxMessageId) noexcept;
    void setLastReadOutboxMessageId(qlonglong lastReadOutboxMessageId) noexcept;
    void setUnreadMentionCount(int unreadMentionCount) noexcept;
    void setNotificationSettings(td::td_api::object_ptr<td::td_api::chatNotificationSettings> notificationSettings) noexcept;

signals:
    void chatChanged();

private:
    void setType(td::td_api::object_ptr<td::td_api::ChatType> type) noexcept;

    td::td_api::object_ptr<td::td_api::chat> m_chat;

    qlonglong m_id;
    Type m_type;
    QString m_title;
    bool m_isMarkedAsUnread;
    int m_unreadCount;
    qlonglong m_lastReadInboxMessageId;
    qlonglong m_lastReadOutboxMessageId;
    int m_unreadMentionCount;

    int m_muteFor;
    qlonglong m_typeId;

    // Shared, not owned: StorageManager registers this same instance in its file map
    // so updateFile mutates the object the chat exposes. See registerChatPhoto().
    std::shared_ptr<File> m_file;
    std::unique_ptr<Message> m_lastMessage;

    std::vector<std::unique_ptr<ChatPosition>> m_positions;
};
