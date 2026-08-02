#pragma once

#include "ChatPosition.hpp"

#include <td/telegram/td_api.h>

#include <QObject>
#include <QVariant>

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

    // The emoji of one Emoji::Category, for the picker grid: a list of
    // { unicode, filename }. Skin-tone variants are left out - they are 1875 of the
    // 3773 entries, and a grid holding six of every person is not one anything can
    // be found in. The base emoji is still sent; Telegram renders it per the
    // recipient's default.
    Q_INVOKABLE static QVariantList emojiCategory(int category) noexcept;

    // QML1 has no Clipboard element, so the "Copy" message action goes through here.
    Q_INVOKABLE static void copyToClipboard(const QString &text) noexcept;

    // Copies a downloaded file into the user's own storage so it survives TDLib
    // clearing its cache and shows up in the Gallery. False if it could not be written.
    Q_INVOKABLE static bool saveToGallery(const QString &localPath) noexcept;

    // The same, into MyDocs/meegram/Downloads and under the name the sender gave the
    // file - TDLib's cache name is a file id and means nothing to the user. No default
    // argument on purpose; see the note on replaceEmojiSized above.
    Q_INVOKABLE static bool saveDocument(const QString &localPath, const QString &fileName) noexcept;

    // Hands a downloaded file to whatever Harmattan has registered for its type.
    // False when there is nothing to open it with, so the caller can say so.
    Q_INVOKABLE static bool openFile(const QString &localPath) noexcept;

    // The gallery hands out file:// URLs and TDLib wants a filesystem path. Goes
    // through QUrl rather than stripping the scheme, so percent-encoded characters
    // in a filename survive.
    Q_INVOKABLE static QString toLocalFile(const QString &url) noexcept;

    // The mirror of toLocalFile, for handing a path to anything taking a url -
    // FolderListModel.folder in particular. Percent-encodes, so a directory with a
    // space in its name is still navigable.
    Q_INVOKABLE static QString toFileUrl(const QString &path) noexcept;

    // MyDocs, the user-visible storage the file picker browses - all of it, not just
    // the meegram folder saves go into. The one place on Harmattan that Tracker
    // indexes and the stock apps look at.
    Q_INVOKABLE static QString documentsPath() noexcept;

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

    // The public "@name" a chat can be reached by, empty for the ones that have none
    // (basic groups, private groups, users who never set one). Lives on the chat rather
    // than beside it because the username is the supergroup's or the user's, and the chat
    // is the only thing that knows which of those it is.
    static QString getChatUsername(const std::shared_ptr<Chat> &chat, const std::shared_ptr<StorageManager> &storage) noexcept;
    static QString getUserUsername(const std::shared_ptr<User> &user) noexcept;

    static QString getSenderName(const Message *message, std::shared_ptr<StorageManager> storage) noexcept;
    static QString getSenderAuthor(Message *message, std::shared_ptr<StorageManager> storage, bool openUser) noexcept;

    static QString getUserName(std::shared_ptr<User> user, bool openUser) noexcept;
    static QString getUserShortName(std::shared_ptr<User> user) noexcept;

    static bool isMeUser(std::shared_ptr<User> user, std::shared_ptr<StorageManager> storage) noexcept;
    static bool isMeChat(std::shared_ptr<Chat> chat, std::shared_ptr<StorageManager> storage) noexcept;
};
