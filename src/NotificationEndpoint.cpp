#include "NotificationEndpoint.hpp"

#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include <QWidget>

namespace {

// Matches the Name in resources/com.meegram.service, so a tap on a banner posted while
// the app was closed starts it. Distinct from com.meegram.Daemon, which meegramd owns.
constexpr auto ServiceName = "com.meegram";

// One fixed path, registered at startup. The old implementation registered one object per
// chat as it posted, which only worked because the same process had posted the banner -
// it cannot be reconstructed by a UI that has just been started by the tap it is
// answering.
constexpr auto ObjectPath = "/notification";

}  // namespace

NotificationEndpoint::NotificationEndpoint(QObject *parent)
    : QObject(parent)
{
    auto connection = QDBusConnection::sessionBus();

    // Not fatal, and not latched into a retry either: without the name, dbus-daemon
    // cannot activate this process for a tap, and a second instance racing for it is a
    // problem that a retry would not fix.
    if (!connection.registerService(QLatin1String(ServiceName)))
        qWarning() << "tap-to-open disabled, could not register" << ServiceName;

    if (!connection.registerObject(QLatin1String(ObjectPath), this, QDBusConnection::ExportScriptableSlots))
        qWarning() << "tap-to-open disabled, could not register" << ObjectPath;
}

void NotificationEndpoint::openChat(const QString &chatId)
{
    // Tapped from the lock screen or the event feed, so the window is very likely behind
    // something. There is exactly one top level widget here - the viewer.
    for (auto *widget : QApplication::topLevelWidgets())
    {
        widget->raise();
        widget->activateWindow();
    }

    emit chatRequested(chatId);
}
