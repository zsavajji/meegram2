import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

// Points the camera at the QR code Telegram Desktop (or another client) is showing and
// signs that device in. The opposite direction from QrCodePage, which draws a code for
// *this* device to be signed in from somewhere else.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    // Set once a code has been accepted, so a second read of the same code - which is what
    // the next frame holds - does not send a second request.
    property bool handled: false

    TopBar {
        id: header
        title: qsTr("LinkDesktopDevice")
    }

    QrScanner {
        id: scanner

        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
            bottom: hint.top
            bottomMargin: 12
        }

        // The camera is a single device on this phone: a viewfinder left running holds it
        // against every other application, so it starts when the page is shown and stops
        // the moment it is not - including on the way to a dialog.
        active: root.status === PageStatus.Active && !root.handled

        onScanned: {
            // A QR code is a QR code; only a login link is any of our business, and the
            // camera will happily read a poster on the wall behind the screen.
            if (root.handled || text.indexOf("tg://login") !== 0) {
                if (text.indexOf("tg://login") !== 0)
                    appWindow.showInfoBanner(qsTr("AuthAnotherClientNotQr"))

                return;
            }

            root.handled = true;

            appManager.sessionModel.confirmQrLogin(text);
        }

        onFailed: appWindow.showInfoBanner(message)
    }

    Label {
        id: hint

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 16
        }

        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: 24
        color: appWindow.secondaryColor

        text: scanner.available ? qsTr("AuthAnotherClientScan")
                                // Not qsTr: there is no pack key for a device limitation.
                                : "This device has no camera available to scan with."
    }

    // The list behind this page reloads when the login is confirmed, so its own answer is
    // the confirmation - there is nothing to show here but the way back.
    Connections {
        target: appManager.sessionModel

        onCountChanged: {
            if (root.handled)
                appWindow.pageStack.pop();
        }

        onFailed: {
            // Let them try again rather than stranding the page on a failed attempt.
            root.handled = false;
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }
}
