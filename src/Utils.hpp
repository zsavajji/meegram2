#pragma once

#include "ChatPosition.hpp"

#include <td/telegram/td_api.h>

#include <QObject>

class Chat;
class Locale;
class Message;
class MessageAudio;
class MessageCall;
class MessageContent;
class StorageManager;
class User;

class Utils : public QObject
{
    Q_OBJECT
public:
    explicit Utils(QObject *parent = nullptr);

    static td::td_api::object_ptr<td::td_api::ChatList> toChatList(const std::unique_ptr<ChatList> &list) noexcept;

    static QString formattedText(const td::td_api::object_ptr<td::td_api::formattedText> &value) noexcept;

    Q_INVOKABLE static QString formatTime(int totalSeconds) noexcept;

    Q_INVOKABLE static QString replaceEmoji(const QString &text) noexcept;

    // A separate name rather than a default argument on replaceEmoji. A Q_INVOKABLE with
    // a default argument makes moc emit a cloned metamethod, so the same name exists at
    // two arities and QML1 has to resolve between them; if it picks the two-argument
    // entry for a one-argument call, the second slot in the argument array was never
    // filled in. size <= 0 means the default body-text size.
    Q_INVOKABLE static QString replaceEmojiSized(const QString &text, int size) noexcept;

    // The image size a message consisting of nothing but one to three emoji should be
    // drawn at, or 0 for anything else - pass straight to replaceEmoji(). Takes the
    // plain text, not the formatted one, or the markup would count as non-emoji.
    Q_INVOKABLE static int emojiOnlySize(const QString &text) noexcept;

    // QML1 has no Clipboard element, so the "Copy" message action goes through here.
    Q_INVOKABLE static void copyToClipboard(const QString &text) noexcept;

    // Copies a downloaded file into the user's own storage so it survives TDLib
    // clearing its cache and shows up in the Gallery. False if it could not be written.
    Q_INVOKABLE static bool saveToGallery(const QString &localPath) noexcept;

    // The gallery hands out file:// URLs and TDLib wants a filesystem path. Goes
    // through QUrl rather than stripping the scheme, so percent-encoded characters
    // in a filename survive.
    Q_INVOKABLE static QString toLocalFile(const QString &url) noexcept;

    static QString getAudioTitle(MessageAudio *audio) noexcept;
    static QString getCallContent(MessageCall *call, bool isOutgoing) noexcept;

    static QString getMessageDate(Message *message) noexcept;

    static QString getContent(Message *message, std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale) noexcept;

    // Renders content on its own, without a surrounding Message. Needed for reply
    // previews: messageReplyToMessage carries a MessageContent inline, with no
    // Message to hang it off. Service messages are not reachable this way - they
    // need the Message, so the overload above handles them before delegating here.
    // No StorageManager: nothing in this path resolves an entity.
    static QString getContent(MessageContent *content, int contentType, bool isOutgoing, std::shared_ptr<Locale> locale) noexcept;
    static QString getServiceContent(Message *message, std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale, bool openUser = false) noexcept;

    static QString getChatTitle(std::shared_ptr<Chat> chat, std::shared_ptr<StorageManager> storage, bool showSavedMessages = true) noexcept;

    static QString getSenderName(const Message *message, std::shared_ptr<StorageManager> storage) noexcept;
    static QString getSenderAuthor(Message *message, std::shared_ptr<StorageManager> storage, bool openUser) noexcept;

    static QString getUserName(std::shared_ptr<User> user, bool openUser) noexcept;
    static QString getUserShortName(std::shared_ptr<User> user) noexcept;

    static bool isMeUser(std::shared_ptr<User> user, std::shared_ptr<StorageManager> storage) noexcept;
    static bool isMeChat(std::shared_ptr<Chat> chat, std::shared_ptr<StorageManager> storage) noexcept;
};
