#pragma once

#include <QObject>

// The app's entire share of notifications under the daemon transport: one method that
// says "open this chat".
//
// Composing, posting and withdrawing banners is meegramd's job now
// (src/daemon/Notifier.cpp), because a notification that only fires while the app is open
// is the problem docs/restructuring.md exists to solve. What cannot move is raising the
// window, so the daemon calls this - starting the app through com.meegram.service first
// if it is not running, which is the case this whole exercise is about.
//
// The in-process transport keeps src/NotificationManager.cpp instead: no daemon exists
// there to post anything.
class NotificationEndpoint : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.meegram.Notification")

public:
    explicit NotificationEndpoint(QObject *parent = nullptr);

signals:
    // main.qml turns this into a page push. The id is a decimal string for the same
    // reason every Q_INVOKABLE here takes one: a qlonglong crossing into QML1 is boxed as
    // a double, and the way back is the conversion that corrupts.
    void chatRequested(const QString &chatId);

public slots:
    // Called by meegramd when one of its banners is tapped.
    Q_SCRIPTABLE void openChat(const QString &chatId);
};
