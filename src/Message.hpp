#pragma once

#include "MessageContent.hpp"

#include <QDateTime>

#include <memory>

class Message : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qlonglong id READ id CONSTANT)
    // chatId and senderId are deliberately not Q_PROPERTY - see the note in Chat.hpp.
    Q_PROPERTY(bool isOutgoing READ isOutgoing CONSTANT)
    Q_PROPERTY(QDateTime date READ date CONSTANT)
    Q_PROPERTY(QDateTime editDate READ editDate NOTIFY messageChanged)

    Q_PROPERTY(QString contentType READ contentTypeString NOTIFY messageChanged)
    Q_PROPERTY(bool isService READ isService CONSTANT)

public:
    explicit Message(td::td_api::object_ptr<td::td_api::message> message, QObject *parent = nullptr);

    enum class SenderType { Unknown, Chat, User };

    // What a reply points at. Plain data rather than a QObject: the strings QML
    // actually needs (sender name, preview) require a StorageManager and a Locale to
    // produce, so MessageModel formats them and exposes them as roles.
    //
    // `origin` is only populated by TDLib when the replied-to message came from
    // elsewhere. For an ordinary same-chat reply every sender field stays empty and
    // MessageModel falls back to looking the message up among the ones it has loaded.
    struct ReplyInfo
    {
        qlonglong messageId{0};
        qlonglong senderUserId{0};
        qlonglong senderChatId{0};
        QString hiddenSenderName;
        QString quote;

        int contentType{0};
        std::unique_ptr<MessageContent> content;
    };

    qlonglong id() const;
    qlonglong chatId() const;
    qlonglong senderId() const;
    bool isOutgoing() const;
    QDateTime date() const;
    QDateTime editDate() const;
    MessageContent *content() const;

    bool isService() const noexcept;

    // TDLib carries a sending state only while the message is on its way out: null means
    // the server has it. Both are false for anything incoming.
    bool isPending() const noexcept;
    bool isFailed() const noexcept;

    // Null when this message is not a reply.
    const ReplyInfo *replyTo() const noexcept;

    // Whether this message is one of the chat's unread mentions. TDLib only clears a
    // mention for the ids viewMessages is handed - the read pointer does nothing for it -
    // so MessageModel has to know which of the loaded rows still carry one. Cleared as
    // soon as the id goes out, so scrolling does not keep resending the same message.
    bool containsUnreadMention() const noexcept;
    void clearUnreadMention() noexcept;

    // The reactions on this message, or null when nobody has reacted. Handed out as the
    // td_api object rather than copied into a struct of its own: MessageModel is the only
    // reader and it walks this straight into a role, so a parallel type would be a second
    // spelling of the same thing - unlike ReplyInfo above, which exists because the reply
    // has to survive the update that replaces it.
    const td::td_api::messageReactions *reactions() const noexcept;

    int contentType() const;
    QString contentTypeString() const;

    // Non-zero when the message was sent as part of an album - every message of the batch
    // carries the same value. Deliberately not a Q_PROPERTY: a qlonglong crossing into
    // QML1 is the corruption trap described in Chat.hpp, so MessageModel groups the rows
    // in C++ and QML never sees the id.
    qlonglong mediaAlbumId() const noexcept;

    SenderType senderType() const;

    void setContent(td::td_api::object_ptr<td::td_api::MessageContent> content);
    void setReplyTo(td::td_api::object_ptr<td::td_api::MessageReplyTo> replyTo);
    void setEditDate(int editDate);

    // Replaces the whole block wholesale, which is what updateMessageInteractionInfo
    // delivers - including a null one, when the last reaction was taken away.
    void setInteractionInfo(td::td_api::object_ptr<td::td_api::messageInteractionInfo> interactionInfo);

signals:
    void messageChanged();

private:
    qlonglong m_id;
    qlonglong m_chatId;
    qlonglong m_senderId;
    bool m_isOutgoing;
    bool m_isPending;
    bool m_isFailed;
    QDateTime m_date;
    QDateTime m_editDate;

    int m_contentType;
    SenderType m_senderType;
    qlonglong m_mediaAlbumId;

    std::unique_ptr<MessageContent> m_content;
    std::unique_ptr<ReplyInfo> m_replyTo;

    td::td_api::object_ptr<td::td_api::message> m_message;
};

Q_DECLARE_METATYPE(Message *);
