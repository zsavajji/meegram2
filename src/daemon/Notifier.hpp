#pragma once

#include <dbus/dbus.h>

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace td
{
class JsonObject;
}

// Posts Harmattan system notifications, in the process that outlives the UI.
//
// This used to live in the app (src/NotificationManager.cpp) and could therefore only fire
// while the app was open, which is the thing docs/restructuring.md set out to fix. The
// whole of it is here now: what to show, when to withdraw it, and what a tap does. The UI
// keeps one method - com.meegram /notification openChat - and no notification state at
// all.
//
// The logic is thinner than the version it replaces, because it is driven by TDLib's own
// notification updates rather than by raw chat updates. TDLib already decides what is
// worth a banner: it applies the chat's notification settings and the scope defaults,
// ignores outgoing and service messages, does not renotify a message read on another
// device, and removes a notification when its message is read anywhere. Reimplementing
// that here - which is what a straight port of NotificationManager would have been -
// would have meant a copy of StorageManager in a process that deliberately has no model.
//
// Enabled by setting notification_group_count_max, which TDLib defaults to 0, meaning
// "client does not show notifications" (td/telegram/NotificationManager.h). Nothing had
// ever set it, so the update stream this feeds on did not exist until now.
class Notifier
{
    // tools/notifier_check.cpp. What is worth checking here - a preview composed from a
    // notification, a language pack string replacing its fallback, a chat photo tracked
    // to its download - is all private, and the alternative to one friend line is a
    // public surface that exists only for a test.
    friend struct NotifierTest;

public:
    // One line of JSON to td_send. Requests carry an "@extra" that starts with
    // "meegramd-", which no UI claims - see the note in Client::send.
    using Sender = std::function<void(const std::string &)>;

    explicit Notifier(Sender send);
    ~Notifier();

    Notifier(const Notifier &) = delete;
    Notifier &operator=(const Notifier &) = delete;

    // Registers the tap handler on the connection that owns com.meegram.Daemon. Messages
    // for it are dispatched by the poll loop in main, on that thread; everything else
    // here runs on the receive thread, hence the mutex.
    void attachTapHandler(DBusConnection *connection);

    // A line from td_receive, already relayed to the UIs.
    void onUpdate(const char *line, size_t length);

    // A line a UI sent to TDLib, already relayed. Only openChat and closeChat are of
    // interest: they are how this process knows what the user is looking at.
    void onRequest(const std::string &line);

private:
    // What a banner needs beyond the notification itself. Filled from the update stream -
    // updateNewChat arrives long before any notification for that chat - so no request is
    // made for it and no response has to be waited on.
    struct ChatInfo
    {
        std::string title;
        std::string photoPath;
        int photoFileId{0};
        bool photoDownloadable{false};
        bool photoDownloading{false};
        // Private and secret chats say who spoke in the summary, so their body does not
        // repeat it.
        bool isPrivate{false};
    };

    struct Banner
    {
        long long chatId{0};
        dbus_uint32_t id{0};
    };

    void handleNotificationGroup(td::JsonObject &update);
    void handleNotification(td::JsonObject &update);
    void handleNewChat(td::JsonObject &update);
    void handleFile(td::JsonObject &update);
    void handleUser(td::JsonObject &update);
    void handleOption(td::JsonObject &update);
    void handleLanguagePack(td::JsonObject &response);

    // Composes and posts, or updates the banner already on screen for this group.
    void show(int groupId, long long chatId, td::JsonObject &notification);
    void withdrawGroup(int groupId);
    void withdrawChat(long long chatId);

    // The one-line preview under the chat title. Mirrors Utils::getContent, minus the
    // service-message half: TDLib does not raise notifications for those.
    std::string describe(td::JsonObject &notification, const ChatInfo &chat);
    std::string preview(td::JsonObject &content);
    std::string senderName(td::JsonObject &message);

    // Language pack string, or the English fallback baked in below it. Both processes
    // read the same pack from TDLib; this one only needs the dozen keys a preview uses.
    std::string translate(const char *key) const;
    void requestLanguagePack();

    dbus_uint32_t notificationUserId();
    dbus_uint32_t addNotification(const std::string &summary, const std::string &body, const std::string &action, const std::string &image);
    bool updateNotification(dbus_uint32_t id, const std::string &summary, const std::string &body, const std::string &action,
                            const std::string &image);
    void removeNotification(dbus_uint32_t id);

    // Answers the tap. Static because libdbus wants a plain function pointer.
    static DBusHandlerResult handleTapMessage(DBusConnection *connection, DBusMessage *message, void *data);
    void activate(long long chatId);

    Sender m_send;

    // Everything below is touched by the receive thread and by the poll thread when a
    // banner is tapped.
    // ponytail: one lock for the whole notifier. Per-map locks buy nothing at a few
    // notifications a minute; revisit if this ever sits on the sync path.
    std::mutex m_mutex;

    std::unordered_map<long long, ChatInfo> m_chats;

    // ponytail: unbounded, and filled from every updateUser TDLib sends. In practice that
    // is people who have appeared in a loaded chat, not every member of every group -
    // a few thousand entries, a few hundred KB. If a large account ever makes that show
    // up in the daemon's RSS, cache only senders and ask getUser for the rest.
    std::unordered_map<long long, std::string> m_userNames;
    std::unordered_map<int, long long> m_photoOwners;
    std::unordered_map<int, Banner> m_banners;
    std::unordered_map<std::string, std::string> m_strings;

    // The chat the user has open, learned from the openChat and closeChat a UI relays
    // through here. Nothing is shown for it: the UI is looking at the messages.
    long long m_openChatId{0};

    std::string m_languagePackId;

    DBusConnection *m_bus{nullptr};

    dbus_uint32_t m_userId{0};
    bool m_userIdResolved{false};
};
