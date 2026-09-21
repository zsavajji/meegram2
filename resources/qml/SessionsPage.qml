import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

// Every device this account is signed in on, and the way to sign one of them out.
//
// There is no update behind this list - TDLib has no updateActiveSessions - so it is what
// the last getActiveSessions said. It reloads when the page opens and after every
// terminate, which is the only thing here that changes it.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant model: appManager.sessionModel

    TopBar {
        id: header
        title: qsTr("Devices")
    }

    ListView {
        id: listView

        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
            bottom: parent.bottom
        }

        // MUST on every list here: without it the rows paint over the toolbar.
        clip: true
        cacheBuffer: 1000

        model: root.model

        // The current session sorts first (SessionModel::load), so this is a header for
        // row 0 and a second one for everything after it - which is what makes "this
        // device" and "everything else" read as two groups without a section role.
        section.property: "isCurrent"
        section.delegate: SectionHeader {
            text: section === "true" ? qsTr("CurrentSession") : qsTr("OtherSessions")
        }

        delegate: ListItem {
            id: row

            // Taller than a stock row: three lines of detail, which is what makes a
            // session identifiable at all - two devices of the same model differ only by
            // where they are and when they were last used.
            height: 64 + detail.height + 24

            // The current session is not terminable: TDLib refuses it, and what it would
            // mean is logging out - which is its own row on the account page.
            onClicked: {
                if (model.isCurrent)
                    return;

                internal.pendingId = model.id;
                internal.pendingName = model.application;
                terminateDialog.open();
            }

            Column {
                id: detail

                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: 12
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }

                spacing: 2

                Label {
                    width: parent.width
                    elide: Text.ElideRight
                    font.pixelSize: 26
                    font.bold: true
                    text: model.application
                }

                Label {
                    width: parent.width
                    elide: Text.ElideRight
                    font.pixelSize: 22
                    color: appWindow.secondaryColor
                    // Device and system on one line: "Nokia N9 - MeeGo 1.2 Harmattan".
                    // Either half can be empty, so the separator is conditional.
                    text: model.device !== "" && model.system !== "" ? model.device + " - " + model.system
                                                                     : model.device + model.system
                }

                Label {
                    width: parent.width
                    elide: Text.ElideRight
                    font.pixelSize: 22
                    color: appWindow.secondaryColor
                    // Where and when. The IP is the fallback for a session TDLib has no
                    // location for, because "somewhere" is worse than an address.
                    text: (model.location !== "" ? model.location : model.ipAddress) +
                          (model.lastActive !== "" ? " - " + model.lastActive : "")
                }
            }
        }

        ScrollDecorator { flickableItem: listView }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: root.model.loading && root.model.count === 0
        visible: running
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 32
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: appWindow.secondaryColor
        font.pixelSize: 24
        // Only once the answer is in: an empty list before that is "not loaded yet".
        visible: !root.model.loading && root.model.count === 0
        text: qsTr("NoOtherSessions")
    }

    QueryDialog {
        id: terminateDialog

        titleText: qsTr("TerminateSessionQuestion")
        message: internal.pendingName
        acceptButtonText: qsTr("OK")
        rejectButtonText: qsTr("Cancel")

        onAccepted: root.model.terminate(internal.pendingId)
    }

    QueryDialog {
        id: terminateAllDialog

        titleText: qsTr("TerminateAllSessions")
        message: qsTr("AreYouSureSessions")
        acceptButtonText: qsTr("OK")
        rejectButtonText: qsTr("Cancel")

        onAccepted: root.model.terminateOthers()
    }

    Menu {
        id: menu

        MenuLayout {
            MenuItem {
                text: qsTr("LinkDesktopDevice")
                onClicked: appWindow.pageStack.push(Qt.createComponent("QrScannerPage.qml"))
            }

            MenuItem {
                text: qsTr("TerminateAllSessions")
                // Nothing to end when this device is the only one signed in.
                visible: root.model.count > 1
                onClicked: terminateAllDialog.open()
            }
        }
    }

    Connections {
        target: root.model
        onFailed: appWindow.showInfoBanner(message)
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }

        ToolIcon {
            platformIconId: "toolbar-view-menu"
            onClicked: menu.open()
        }
    }

    Component.onCompleted: root.model.load()

    QtObject {
        id: internal

        // The row the dialog is about. Captured on tap rather than read back from the view:
        // the list reloads underneath it after any terminate, and a dialog bound to an
        // index would be asking about whatever moved into that row.
        property string pendingId: ""
        property string pendingName: ""
    }
}
