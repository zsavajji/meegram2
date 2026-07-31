#include "Notifier.hpp"

#include "td/utils/JsonBuilder.h"
#include "td/utils/Slice.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace {

// libmeegotouch's MNotification talks to this; there is no freedesktop.Notifications
// service on Harmattan. Same three calls the app used to make, over libdbus instead of
// QtDBus - this process has no Qt.
constexpr auto NotificationService = "com.meego.core.MNotificationManager";
constexpr auto NotificationPath = "/notificationmanager";
constexpr auto NotificationInterface = "com.meego.core.MNotificationManager";

// One of the event types under /usr/share/meegotouch/notifications/eventtypes. It picks
// the icon, sound and vibration; this is the platform's instant-message one.
constexpr auto EventType = "x-nokia.messaging.im";

// Where a tap lands. It used to be the app, which meant a tap on a banner raised by a
// client that had since exited went nowhere. It is this process now - always running, by
// construction - and it forwards to the UI, starting it if it is not up.
constexpr auto DaemonService = "com.meegram.Daemon";
constexpr auto TapInterface = "com.meegram.Daemon.Notification";
constexpr auto TapMethod = "activate";
constexpr auto TapPathPrefix = "/chat/";

// The UI's half of tap-to-open: one method, one fixed path, registered at startup rather
// than per chat. The chat id is an argument here because this end is a D-Bus call the
// daemon builds, not an MRemoteAction string whose argument encoding is undocumented.
constexpr auto UiService = "com.meegram";
constexpr auto UiPath = "/notification";
constexpr auto UiInterface = "com.meegram.Notification";
constexpr auto UiMethod = "openChat";

// Short, because these are blocking calls on the thread that drains td_receive: a wedged
// notification daemon must not be able to stall the relay for QtDBus's 25 seconds.
constexpr int CallTimeoutMs = 2000;

// How many chats may have notifications at once. TDLib defaults this to 0, which means
// "do not generate notifications at all" - setting it is what turns the whole subsystem
// on. 5 is the platform's practical limit for banners on screen; the cap only bounds how
// many *groups* TDLib tracks, not how many messages are in one.
constexpr auto NotificationGroupCountMax = 5;

// TDLib writes "@type" first (ClientJson::to_response and the client-direction codec both
// do), so the type is a prefix test and an uninteresting line costs a memcmp rather than a
// parse. That matters: initial sync pushes thousands of updates through here.
constexpr char TypePrefix[] = "{\"@type\":\"";

// The type name, without decoding the line. Empty if this is not a line of the shape
// TDLib produces, which is nothing this process should be acting on anyway.
std::string_view typeName(const char *line, size_t length)
{
    constexpr size_t prefixLength = sizeof(TypePrefix) - 1;

    if (length < prefixLength || std::memcmp(line, TypePrefix, prefixLength) != 0)
        return {};

    const char *begin = line + prefixLength;

    const void *end = std::memchr(begin, '"', length - prefixLength);
    if (!end)
        return {};

    return std::string_view(begin, static_cast<const char *>(end) - begin);
}

// The Result<> accessors abort on an error, so every read goes through one of these.
std::string stringField(const td::JsonObject &object, td::Slice name)
{
    auto result = object.get_optional_string_field(name);
    return result.is_ok() ? result.move_as_ok() : std::string();
}

long long longField(const td::JsonObject &object, td::Slice name)
{
    auto result = object.get_optional_long_field(name);
    return result.is_ok() ? static_cast<long long>(result.move_as_ok()) : 0;
}

int intField(const td::JsonObject &object, td::Slice name)
{
    auto result = object.get_optional_int_field(name);
    return result.is_ok() ? result.move_as_ok() : 0;
}

bool boolField(const td::JsonObject &object, td::Slice name, bool defaultValue = false)
{
    auto result = object.get_optional_bool_field(name, defaultValue);
    return result.is_ok() ? result.move_as_ok() : defaultValue;
}

// Moves a nested object out of its parent. Empty if the field is absent or of another
// type, which every caller then reads as absent fields - the same outcome, without a
// branch per field.
td::JsonObject takeObject(td::JsonObject &object, td::Slice name)
{
    auto value = object.extract_field(name);
    if (value.type() != td::JsonValue::Type::Object)
        return td::JsonObject();

    return std::move(value.get_object());
}

std::string typeOf(const td::JsonObject &object)
{
    return stringField(object, "@type");
}

// The keys a preview needs, with what to show when the language pack has not arrived -
// or has arrived without them, which is what an incomplete translation looks like.
// Same keys the app's Utils::getContent uses, so both ends say the same thing.
struct Translation
{
    const char *key;
    const char *fallback;
};

constexpr Translation Translations[] = {
    {"AttachPhoto", "Photo"},
    {"AttachVideo", "Video"},
    {"AttachAudio", "Voice message"},
    {"AttachMusic", "Music"},
    {"AttachGif", "GIF"},
    {"AttachSticker", "Sticker"},
    {"AttachDocument", "File"},
    {"AttachLocation", "Location"},
    {"AttachContact", "Contact"},
    {"AttachRound", "Video message"},
    {"AttachGame", "Game"},
    {"UnsupportedAttachment", "Unsupported attachment"},
    {"CallMessageIncoming", "Incoming call"},
    {"EncryptedChat", "Secret chat"},
    {"YouHaveNewMessage", "You have a new message"},
};

// Language pack ids are tags like "en" or "pt-br". Anything else is not one, and this
// string is about to be pasted into a JSON request - so it is checked rather than escaped.
bool isLanguagePackId(const std::string &id)
{
    if (id.empty() || id.size() > 32)
        return false;

    return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string objectPathFor(long long chatId)
{
    // Object path components accept [A-Za-z0-9_] only, and supergroup ids are negative.
    // Same encoding the app used, kept so a banner posted by an older build still
    // decodes.
    char suffix[32];
    if (chatId < 0)
        std::snprintf(suffix, sizeof(suffix), "n%lld", -chatId);
    else
        std::snprintf(suffix, sizeof(suffix), "%lld", chatId);

    return std::string(TapPathPrefix) + suffix;
}

long long chatIdFromPath(const char *path)
{
    const size_t prefixLength = std::strlen(TapPathPrefix);
    if (!path || std::strncmp(path, TapPathPrefix, prefixLength) != 0)
        return 0;

    const char *tail = path + prefixLength;

    return *tail == 'n' ? -std::strtoll(tail + 1, nullptr, 10) : std::strtoll(tail, nullptr, 10);
}

// The MRemoteAction wire format: service, object path, interface and method, space
// separated.
std::string remoteAction(long long chatId)
{
    return std::string(DaemonService) + ' ' + objectPathFor(chatId) + ' ' + TapInterface + ' ' + TapMethod;
}

}  // namespace

Notifier::Notifier(Sender send)
    : m_send(std::move(send))
{
    DBusError error;
    dbus_error_init(&error);

    // Private, not the shared connection main uses for the bus name: this one is called
    // from the receive thread and from the poll thread, and sharing the dispatch state
    // with the name-owning connection would mean two threads inside one connection's
    // message queue for no gain.
    m_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!m_bus)
    {
        std::fprintf(stderr, "meegramd: no session bus for notifications (%s)\n", dbus_error_is_set(&error) ? error.message : "unknown");
        dbus_error_free(&error);
    }
    else
    {
        // Nothing here treats losing the bus as fatal, and the default action is to
        // exit() from inside dispatch - which would take the TDLib connection with it.
        dbus_connection_set_exit_on_disconnect(m_bus, FALSE);
    }

    // Without this TDLib generates no notifications at all. Sent again on
    // authorizationStateReady, because a client that is not authorized yet has no
    // notification manager to receive it.
    m_send(R"({"@type":"setOption","name":"notification_group_count_max","value":{"@type":"optionValueInteger","value":)" +
           std::to_string(NotificationGroupCountMax) + R"(},"@extra":"meegramd-option"})");
}

Notifier::~Notifier()
{
    if (m_bus)
    {
        dbus_connection_close(m_bus);
        dbus_connection_unref(m_bus);
    }
}

void Notifier::attachTapHandler(DBusConnection *connection)
{
    if (!connection)
        return;

    // A fallback rather than one object per chat: the path carries the chat id, so the
    // set of paths is not known in advance and registering them one at a time would mean
    // tracking which are live across a restart.
    static const DBusObjectPathVTable vtable = {nullptr, &Notifier::handleTapMessage, nullptr, nullptr, nullptr, nullptr};

    if (!dbus_connection_register_fallback(connection, "/chat", &vtable, this))
        std::fprintf(stderr, "meegramd: cannot register the tap handler; banners will not open the app\n");
}

void Notifier::onUpdate(const char *line, size_t length)
{
    // updateActiveNotifications is deliberately not in this list. TDLib sends it once,
    // at startup, carrying every notification still live in its database - which after a
    // busy night is one per chat, and posting them all is a banner storm for messages
    // that arrived while nothing was running. The app's version filtered the same replay
    // by comparing message dates against its own start time; not listening at all is the
    // same rule with nothing to get wrong.
    //
    // The cost is that a daemon restart forgets the banners it had already posted: their
    // platform ids are gone, so those cannot be withdrawn and the user has to swipe them
    // away. Reading updateActiveNotifications would not fix that either - the ids it
    // carries are TDLib's, not the notification manager's.
    const auto name = typeName(line, length);

    const bool interesting = name == "updateNotificationGroup" || name == "updateNewChat" || name == "updateFile" ||
                             name == "updateUser" || name == "updateNotification" || name == "updateChatTitle" ||
                             name == "updateChatPhoto" || name == "updateOption" || name == "updateAuthorizationState" ||
                             name == "languagePackStrings";
    if (!interesting)
        return;

    // json_decode unescapes in place, so it needs a buffer of its own: td_receive's
    // belongs to TDLib and has already been handed to the relay.
    std::string buffer(line, length);

    auto decoded = td::json_decode(td::MutableSlice(buffer));
    if (decoded.is_error() || decoded.ok().type() != td::JsonValue::Type::Object)
        return;

    auto value = decoded.move_as_ok();
    auto &update = value.get_object();

    const std::string type = typeOf(update);

    const std::lock_guard<std::mutex> lock(m_mutex);

    if (type == "updateNotificationGroup")
    {
        handleNotificationGroup(update);
    }
    else if (type == "updateNotification")
    {
        handleNotification(update);
    }
    else if (type == "updateNewChat")
    {
        handleNewChat(update);
    }
    else if (type == "updateChatTitle")
    {
        m_chats[longField(update, "chat_id")].title = stringField(update, "title");
    }
    else if (type == "updateChatPhoto")
    {
        const auto chatId = longField(update, "chat_id");
        auto photo = takeObject(update, "photo");
        auto &chat = m_chats[chatId];

        auto small = takeObject(photo, "small");
        auto local = takeObject(small, "local");

        chat.photoFileId = intField(small, "id");
        chat.photoPath = boolField(local, "is_downloading_completed") ? stringField(local, "path") : std::string();
        chat.photoDownloadable = boolField(local, "can_be_downloaded");
        chat.photoDownloading = boolField(local, "is_downloading_active");

        if (chat.photoFileId != 0)
            m_photoOwners[chat.photoFileId] = chatId;
    }
    else if (type == "updateUser")
    {
        handleUser(update);
    }
    else if (type == "updateFile")
    {
        handleFile(update);
    }
    else if (type == "updateOption")
    {
        handleOption(update);
    }
    else if (type == "updateAuthorizationState")
    {
        auto state = takeObject(update, "authorization_state");
        if (typeOf(state) == "authorizationStateReady")
        {
            m_send(R"({"@type":"setOption","name":"notification_group_count_max","value":{"@type":"optionValueInteger","value":)" +
                   std::to_string(NotificationGroupCountMax) + R"(},"@extra":"meegramd-option"})");

            requestLanguagePack();
        }
    }
    else if (type == "languagePackStrings")
    {
        // A UI asks for the whole pack at startup and that response comes past here too.
        // Ours is the one carrying our "@extra"; taking the other would put several
        // thousand strings into this process for the fifteen it uses.
        if (stringField(update, "@extra") == "meegramd-langpack")
            handleLanguagePack(update);
    }
}

void Notifier::onRequest(const std::string &line)
{
    const auto name = typeName(line.c_str(), line.size());

    const bool isOpen = name == "openChat";
    const bool isClose = name == "closeChat";

    if (!isOpen && !isClose)
        return;

    std::string buffer(line);

    auto decoded = td::json_decode(td::MutableSlice(buffer));
    if (decoded.is_error() || decoded.ok().type() != td::JsonValue::Type::Object)
        return;

    auto value = decoded.move_as_ok();
    const auto chatId = longField(value.get_object(), "chat_id");

    const std::lock_guard<std::mutex> lock(m_mutex);

    if (isClose)
    {
        if (m_openChatId == chatId)
            m_openChatId = 0;
        return;
    }

    m_openChatId = chatId;

    // Whatever is on screen for it is stale the moment the user is looking at the chat.
    withdrawChat(chatId);
}

void Notifier::handleNotificationGroup(td::JsonObject &update)
{
    const auto groupId = intField(update, "notification_group_id");
    const auto chatId = longField(update, "chat_id");
    const auto totalCount = intField(update, "total_count");

    // Empty means everything in it was read, dismissed, or deleted - here or on another
    // device. TDLib works that out; this end only has to take the banner down.
    if (totalCount == 0)
    {
        withdrawGroup(groupId);
        return;
    }

    auto added = update.extract_field("added_notifications");
    if (added.type() != td::JsonValue::Type::Array)
        return;

    auto &notifications = added.get_array();
    if (notifications.empty())
        return;

    // The newest one. A burst arrives as several notifications in one update and the
    // platform shows one banner per chat, updated in place - so the last is what the user
    // reads, and the ones before it were never on screen to be missed.
    auto &newest = notifications.back();
    if (newest.type() != td::JsonValue::Type::Object)
        return;

    show(groupId, chatId, newest.get_object());
}

void Notifier::handleNotification(td::JsonObject &update)
{
    // An edit to a notification already in a group - a message edited before it was read.
    // The group tells us the chat, which this update does not carry.
    const auto groupId = intField(update, "notification_group_id");

    const auto banner = m_banners.find(groupId);
    if (banner == m_banners.end())
        return;

    auto notification = takeObject(update, "notification");
    if (notification.field_count() == 0)
        return;

    show(groupId, banner->second.chatId, notification);
}

void Notifier::handleNewChat(td::JsonObject &update)
{
    auto chatObject = takeObject(update, "chat");
    if (chatObject.field_count() == 0)
        return;

    const auto chatId = longField(chatObject, "id");
    if (chatId == 0)
        return;

    auto &chat = m_chats[chatId];

    chat.title = stringField(chatObject, "title");

    const auto chatType = typeOf(takeObject(chatObject, "type"));
    chat.isPrivate = chatType == "chatTypePrivate" || chatType == "chatTypeSecret";

    auto photo = takeObject(chatObject, "photo");
    auto small = takeObject(photo, "small");
    auto local = takeObject(small, "local");

    chat.photoFileId = intField(small, "id");
    chat.photoPath = boolField(local, "is_downloading_completed") ? stringField(local, "path") : std::string();
    chat.photoDownloadable = boolField(local, "can_be_downloaded");
    chat.photoDownloading = boolField(local, "is_downloading_active");

    if (chat.photoFileId != 0)
        m_photoOwners[chat.photoFileId] = chatId;
}

void Notifier::handleFile(td::JsonObject &update)
{
    auto file = takeObject(update, "file");
    const auto fileId = intField(file, "id");

    const auto owner = m_photoOwners.find(fileId);
    if (owner == m_photoOwners.end())
        return;

    auto local = takeObject(file, "local");

    auto &chat = m_chats[owner->second];

    chat.photoDownloading = boolField(local, "is_downloading_active");

    // Only when it is finished. TDLib fills the path in as soon as a download starts, and
    // a half-written file draws the broken-image square rather than an avatar.
    if (boolField(local, "is_downloading_completed"))
        chat.photoPath = stringField(local, "path");
}

void Notifier::handleUser(td::JsonObject &update)
{
    auto user = takeObject(update, "user");

    const auto userId = longField(user, "id");
    if (userId == 0)
        return;

    auto name = stringField(user, "first_name");
    const auto lastName = stringField(user, "last_name");

    if (!lastName.empty())
    {
        if (!name.empty())
            name += ' ';

        name += lastName;
    }

    m_userNames[userId] = std::move(name);
}

void Notifier::handleOption(td::JsonObject &update)
{
    if (stringField(update, "name") != "language_pack_id")
        return;

    auto value = takeObject(update, "value");
    auto id = stringField(value, "value");

    if (id.empty() || id == m_languagePackId)
        return;

    m_languagePackId = std::move(id);

    requestLanguagePack();
}

void Notifier::handleLanguagePack(td::JsonObject &response)
{
    auto strings = response.extract_field("strings");
    if (strings.type() != td::JsonValue::Type::Array)
        return;

    for (auto &entry : strings.get_array())
    {
        if (entry.type() != td::JsonValue::Type::Object)
            continue;

        auto &languageString = entry.get_object();

        const auto key = stringField(languageString, "key");
        auto value = takeObject(languageString, "value");

        // Ordinary strings only. Nothing a preview shows is pluralised - the one place
        // the app pluralises is the call duration, which a banner does not carry.
        if (key.empty() || typeOf(value) != "languagePackStringValueOrdinary")
            continue;

        m_strings[key] = stringField(value, "value");
    }
}

void Notifier::show(int groupId, long long chatId, td::JsonObject &notification)
{
    // Open and on screen. The app tells us by relaying openChat, and stops telling us
    // when it is no longer in front - ChatManager reopens and recloses the chat around
    // window activation for exactly this reason.
    if (chatId == m_openChatId)
        return;

    const auto &chat = m_chats[chatId];

    const auto body = describe(notification, chat);
    if (body.empty())
        return;

    // The avatar, as a plain absolute path: MNotification::setImage() takes "a path to an
    // image file or an icon id", and a file:// URL draws the broken-image square.
    if (chat.photoPath.empty() && chat.photoDownloadable && !chat.photoDownloading && chat.photoFileId != 0)
    {
        // Nothing to show this time. A notification can arrive for a chat the list never
        // scrolled to, so its avatar was never fetched - ask now, and the next one has it.
        m_send(R"({"@type":"downloadFile","file_id":)" + std::to_string(chat.photoFileId) +
               R"(,"priority":1,"offset":0,"limit":0,"synchronous":false,"@extra":"meegramd-download"})");
    }

    const auto userId = notificationUserId();
    if (userId == 0)
    {
        std::fprintf(stderr, "meegramd: notification for chat %lld not posted - no notification user id\n", chatId);
        return;
    }

    const auto action = remoteAction(chatId);

    // Replace the chat's existing banner rather than stacking a second one.
    if (const auto existing = m_banners.find(groupId); existing != m_banners.end())
    {
        if (updateNotification(existing->second.id, chat.title, body, action, chat.photoPath))
        {
            existing->second.chatId = chatId;
            return;
        }

        // The user swiped it away, or the platform daemon restarted. Fall through and
        // post a new one rather than silently dropping the message.
        m_banners.erase(existing);
    }

    auto published = addNotification(chat.title, body, action, chat.photoPath);

    // The avatar is decoration and must never cost the banner. A returned id of zero is a
    // refusal too - "accepted, nothing appeared" is a real failure mode here.
    if (published == 0 && !chat.photoPath.empty())
    {
        std::fprintf(stderr, "meegramd: addNotification refused carrying %s; retrying without\n", chat.photoPath.c_str());
        published = addNotification(chat.title, body, action, std::string());
    }

    if (published == 0)
    {
        std::fprintf(stderr, "meegramd: addNotification failed for chat %lld\n", chatId);
        return;
    }

    m_banners[groupId] = Banner{chatId, published};
}

void Notifier::withdrawGroup(int groupId)
{
    const auto banner = m_banners.find(groupId);
    if (banner == m_banners.end())
        return;

    removeNotification(banner->second.id);

    m_banners.erase(banner);
}

void Notifier::withdrawChat(long long chatId)
{
    for (auto it = m_banners.begin(); it != m_banners.end();)
    {
        if (it->second.chatId != chatId)
        {
            ++it;
            continue;
        }

        removeNotification(it->second.id);

        it = m_banners.erase(it);
    }
}

std::string Notifier::describe(td::JsonObject &notification, const ChatInfo &chat)
{
    auto type = takeObject(notification, "type");
    const auto kind = typeOf(type);

    if (kind == "notificationTypeNewSecretChat")
        return translate("EncryptedChat");

    if (kind == "notificationTypeNewCall")
        return translate("CallMessageIncoming");

    // notificationTypeNewPushMessage is deliberately not handled: it is what
    // processPushNotification produces, and nothing in this client calls that - the
    // connection is live, so messages arrive as messages.
    if (kind != "notificationTypeNewMessage")
        return {};

    // The user asked for no previews on the lock screen. TDLib has already applied that
    // setting; showing the text anyway would be this process overriding it.
    if (!boolField(type, "show_preview", true))
        return translate("YouHaveNewMessage");

    auto message = takeObject(type, "message");
    if (message.field_count() == 0)
        return {};

    auto content = takeObject(message, "content");

    auto body = preview(content);

    // Groups and channels need to say who spoke; a private chat already has it in the
    // summary.
    if (!chat.isPrivate)
    {
        const auto sender = senderName(message);
        if (!sender.empty())
            body = sender + ": " + body;
    }

    return body;
}

std::string Notifier::preview(td::JsonObject &content)
{
    const auto type = typeOf(content);

    // ": caption", or nothing. Matches Utils::getContent's formatCaption.
    const auto caption = [&content]() -> std::string {
        auto text = takeObject(content, "caption");
        const auto value = stringField(text, "text");

        return value.empty() ? value : ": " + value;
    };

    if (type == "messageText")
    {
        auto text = takeObject(content, "text");
        return stringField(text, "text");
    }
    if (type == "messagePhoto")
        return translate("AttachPhoto") + caption();
    if (type == "messageVideo")
        return translate("AttachVideo") + caption();
    if (type == "messageAnimation")
        return translate("AttachGif") + caption();
    if (type == "messageVoiceNote")
        return translate("AttachAudio") + caption();
    if (type == "messageVideoNote")
        return translate("AttachRound");
    if (type == "messageLocation")
        return translate("AttachLocation");
    if (type == "messageContact")
        return translate("AttachContact");
    if (type == "messageGame")
        return translate("AttachGame");
    if (type == "messageCall")
        return translate("CallMessageIncoming");
    if (type == "messageSticker")
    {
        auto sticker = takeObject(content, "sticker");
        const auto emoji = stringField(sticker, "emoji");

        return emoji.empty() ? translate("AttachSticker") : emoji + " " + translate("AttachSticker");
    }
    if (type == "messageAnimatedEmoji")
    {
        // The emoji itself, not the word "Sticker" - this is a one-emoji text message.
        return stringField(content, "emoji");
    }
    if (type == "messageAudio")
    {
        auto audio = takeObject(content, "audio");
        const auto title = stringField(audio, "title");

        return (title.empty() ? translate("AttachMusic") : title) + caption();
    }
    if (type == "messageDocument")
    {
        auto document = takeObject(content, "document");
        const auto fileName = stringField(document, "file_name");

        return (fileName.empty() ? translate("AttachDocument") : fileName) + caption();
    }
    if (type == "messageVenue")
    {
        auto venue = takeObject(content, "venue");
        const auto title = stringField(venue, "title");

        return title.empty() ? translate("AttachLocation") : translate("AttachLocation") + ": " + title;
    }
    if (type == "messagePoll")
    {
        auto poll = takeObject(content, "poll");
        auto question = takeObject(poll, "question");

        return "\xf0\x9f\x93\x8a " + stringField(question, "text");
    }

    return translate("UnsupportedAttachment");
}

std::string Notifier::senderName(td::JsonObject &message)
{
    auto sender = takeObject(message, "sender_id");
    const auto type = typeOf(sender);

    if (type == "messageSenderUser")
    {
        const auto it = m_userNames.find(longField(sender, "user_id"));
        return it == m_userNames.end() ? std::string() : it->second;
    }

    if (type == "messageSenderChat")
    {
        const auto it = m_chats.find(longField(sender, "chat_id"));
        return it == m_chats.end() ? std::string() : it->second.title;
    }

    return {};
}

std::string Notifier::translate(const char *key) const
{
    if (const auto it = m_strings.find(key); it != m_strings.end() && !it->second.empty())
        return it->second;

    for (const auto &translation : Translations)
    {
        if (std::strcmp(translation.key, key) == 0)
            return translation.fallback;
    }

    return key;
}

void Notifier::requestLanguagePack()
{
    if (!isLanguagePackId(m_languagePackId))
        return;

    std::string request = R"({"@type":"getLanguagePackStrings","language_pack_id":")" + m_languagePackId + R"(","keys":[)";

    for (size_t i = 0; i < sizeof(Translations) / sizeof(Translations[0]); ++i)
    {
        if (i != 0)
            request += ',';

        request += '"';
        request += Translations[i].key;
        request += '"';
    }

    request += R"(],"@extra":"meegramd-langpack"})";

    m_send(request);
}

dbus_uint32_t Notifier::notificationUserId()
{
    if (m_userIdResolved)
        return m_userId;

    if (!m_bus)
        return 0;

    DBusMessage *message = dbus_message_new_method_call(NotificationService, NotificationPath, NotificationInterface, "notificationUserId");
    if (!message)
        return 0;

    DBusError error;
    dbus_error_init(&error);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_bus, message, CallTimeoutMs, &error);
    dbus_message_unref(message);

    if (!reply)
    {
        std::fprintf(stderr, "meegramd: notificationUserId failed: %s\n", dbus_error_is_set(&error) ? error.message : "no reply");
        dbus_error_free(&error);
        return 0;
    }

    dbus_uint32_t userId = 0;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &userId, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        userId = 0;
    }

    dbus_message_unref(reply);

    if (userId == 0)
    {
        // Not latched. The platform notification daemon may simply not be up yet when the
        // first message lands, and latching that failure is how the app used to end up
        // silent for a whole session.
        std::fprintf(stderr, "meegramd: notificationUserId returned 0\n");
        return 0;
    }

    m_userIdResolved = true;
    m_userId = userId;

    return m_userId;
}

namespace {

// addNotification and updateNotification take the same nine arguments; the second is the
// group id for one and the notification id for the other.
void appendNotificationArguments(DBusMessage *message, dbus_uint32_t userId, dbus_uint32_t id, const std::string &summary,
                                 const std::string &body, const std::string &action, const std::string &image)
{
    const char *eventType = EventType;
    const char *summaryText = summary.c_str();
    const char *bodyText = body.c_str();
    const char *actionText = action.c_str();
    const char *imageText = image.c_str();
    const char *identifier = "";

    // Group 0 and count 0: this client does not use the platform's own grouping - TDLib
    // has already grouped by chat, and one banner per chat is what is posted.
    dbus_uint32_t count = 0;

    dbus_message_append_args(message, DBUS_TYPE_UINT32, &userId, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &eventType, DBUS_TYPE_STRING,
                             &summaryText, DBUS_TYPE_STRING, &bodyText, DBUS_TYPE_STRING, &actionText, DBUS_TYPE_STRING, &imageText,
                             DBUS_TYPE_UINT32, &count, DBUS_TYPE_STRING, &identifier, DBUS_TYPE_INVALID);
}

}  // namespace

dbus_uint32_t Notifier::addNotification(const std::string &summary, const std::string &body, const std::string &action,
                                        const std::string &image)
{
    if (!m_bus)
        return 0;

    DBusMessage *message = dbus_message_new_method_call(NotificationService, NotificationPath, NotificationInterface, "addNotification");
    if (!message)
        return 0;

    appendNotificationArguments(message, m_userId, 0, summary, body, action, image);

    DBusError error;
    dbus_error_init(&error);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_bus, message, CallTimeoutMs, &error);
    dbus_message_unref(message);

    if (!reply)
    {
        std::fprintf(stderr, "meegramd: addNotification failed: %s\n", dbus_error_is_set(&error) ? error.message : "no reply");
        dbus_error_free(&error);
        return 0;
    }

    dbus_uint32_t published = 0;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &published, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        published = 0;
    }

    dbus_message_unref(reply);

    return published;
}

bool Notifier::updateNotification(dbus_uint32_t id, const std::string &summary, const std::string &body, const std::string &action,
                                  const std::string &image)
{
    if (!m_bus)
        return false;

    DBusMessage *message = dbus_message_new_method_call(NotificationService, NotificationPath, NotificationInterface, "updateNotification");
    if (!message)
        return false;

    appendNotificationArguments(message, m_userId, id, summary, body, action, image);

    DBusError error;
    dbus_error_init(&error);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_bus, message, CallTimeoutMs, &error);
    dbus_message_unref(message);

    if (!reply)
    {
        dbus_error_free(&error);
        return false;
    }

    // Answers false, not an error, for an id it no longer knows.
    dbus_bool_t updated = FALSE;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &updated, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        updated = FALSE;
    }

    dbus_message_unref(reply);

    return updated == TRUE;
}

void Notifier::removeNotification(dbus_uint32_t id)
{
    if (!m_bus || m_userId == 0)
        return;

    DBusMessage *message = dbus_message_new_method_call(NotificationService, NotificationPath, NotificationInterface, "removeNotification");
    if (!message)
        return;

    dbus_message_append_args(message, DBUS_TYPE_UINT32, &m_userId, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);

    // No reply needed, so this one does not block.
    dbus_message_set_no_reply(message, TRUE);

    dbus_connection_send(m_bus, message, nullptr);
    dbus_connection_flush(m_bus);

    dbus_message_unref(message);
}

DBusHandlerResult Notifier::handleTapMessage(DBusConnection *connection, DBusMessage *message, void *data)
{
    if (!dbus_message_is_method_call(message, TapInterface, TapMethod))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const auto chatId = chatIdFromPath(dbus_message_get_path(message));
    if (chatId != 0)
        static_cast<Notifier *>(data)->activate(chatId);

    if (!dbus_message_get_no_reply(message))
    {
        if (DBusMessage *reply = dbus_message_new_method_return(message))
        {
            dbus_connection_send(connection, reply, nullptr);
            dbus_message_unref(reply);
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

void Notifier::activate(long long chatId)
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        withdrawChat(chatId);
    }

    if (!m_bus)
        return;

    DBusMessage *message = dbus_message_new_method_call(UiService, UiPath, UiInterface, UiMethod);
    if (!message)
        return;

    const auto text = std::to_string(chatId);
    const char *chatIdText = text.c_str();

    dbus_message_append_args(message, DBUS_TYPE_STRING, &chatIdText, DBUS_TYPE_INVALID);

    // Auto-start is on by default and is the point: with the app closed, dbus-daemon
    // launches it from com.meegram.service and delivers this once it owns the name. That
    // is the half of tap-to-open the app-side implementation could not have - it had to be
    // running already to have posted the banner in the first place.
    dbus_message_set_no_reply(message, TRUE);

    dbus_connection_send(m_bus, message, nullptr);
    dbus_connection_flush(m_bus);

    dbus_message_unref(message);
}
