import QtQuick 1.1
import com.nokia.meego 1.1
import "components"

// Starting a conversation with somebody who is not in the chat list yet: contacts you
// have never written to, matched on name or username, and anybody else by their public
// username. The search bar in ChatListView only filters chats TDLib has already pushed.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant model: appWindow.chatManager.searchModel

    TopBar {
        id: header
        title: appWindow.tr("NewMessageTitle")
    }

    TextField {
        id: searchField

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            margins: 12
        }

        placeholderText: qsTr("Search")
        platformStyle: TextFieldStyle { paddingRight: clearIcon.width + 16 }
        // A username is lowercase ASCII; predictive text turns it into a word.
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

        // Every keystroke is a server round trip, so coalesce a burst of them - longer
        // than ChatListView's 300ms, which only rescans a model held in memory.
        onTextChanged: searchTimer.restart()

        Keys.onReturnPressed: {
            searchTimer.stop();
            root.model.search(searchField.text);
            closeSoftwareInputPanel();
        }

        Image {
            id: clearIcon

            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            source: "image://theme/icon-m-input-clear"
            visible: searchField.text.length > 0

            MouseArea {
                // The icon is smaller than a fingertip; the margins are the touch
                // target, not the artwork.
                anchors { fill: parent; margins: -12 }
                onClicked: searchField.text = ""
            }
        }
    }

    Timer {
        id: searchTimer

        interval: 600
        onTriggered: root.model.search(searchField.text)
    }

    ListView {
        id: listView

        anchors {
            top: searchField.bottom
            topMargin: 12
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: root.model

        delegate: ListItem {
            Image {
                id: profilePhotoImage

                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                width: 64
                height: 64

                sourceSize.width: width
                sourceSize.height: height
                asynchronous: true
                fillMode: Image.PreserveAspectCrop

                // Bound to the File itself, which notifies when its download finishes -
                // which is what most rows here are waiting for, unlike the chat list where
                // the avatar is long since cached. isDownloadingCompleted, not localPath:
                // TDLib fills the path in as soon as the download starts.
                source: model.photo && model.photo.isDownloadingCompleted ?
                            "image://chatPhoto/" + model.photo.localPath :
                            "image://theme/icon-l-content-avatar-placeholder"
            }

            Column {
                anchors {
                    left: profilePhotoImage.right
                    leftMargin: 16
                    right: parent.right
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    font.pixelSize: 26
                    font.bold: true
                    elide: Text.ElideRight
                    text: model.title
                }

                Label {
                    width: parent.width
                    font.pixelSize: 20
                    font.family: "Nokia Pure Light"
                    elide: Text.ElideRight
                    text: model.username
                    // A contact reached by phone number has none, and Column leaves out
                    // what it cannot see rather than an empty line.
                    visible: text !== ""
                }
            }

            // Left on the stack rather than popped: the chat is pushed on top, so Back
            // returns to these results, which is what a mistyped username needs.
            onClicked: appWindow.openChat(model.id)
        }
    }

    ScrollDecorator {
        flickableItem: listView
    }

    BusyIndicator {
        anchors.centerIn: listView
        running: visible
        visible: root.model.loading
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    Label {
        anchors.centerIn: listView
        color: "#505050"
        visible: root.model.searched && !root.model.loading && root.model.count === 0
        text: qsTr("NoResult")
    }

    Menu {
        id: pageMenu

        MenuLayout {
            MenuItem {
                text: "Find contacts on Telegram"
                onClicked: importDialog.open()
            }
        }
    }

    // Stated plainly, because this is not a lookup: the numbers leave the device and the
    // matches join the Telegram contact list on every client signed into this account.
    // The official apps do the same thing on first launch - they just do not ask.
    QueryDialog {
        id: importDialog

        titleText: "Find contacts on Telegram"
        message: "Your phone's contact numbers will be sent to Telegram, and the ones with " +
                 "an account will be added to your Telegram contacts on all your devices."
        acceptButtonText: qsTr("OK")
        rejectButtonText: qsTr("Cancel")

        onAccepted: {
            // Clearing the field restarts the debounce, so stop it after - otherwise it
            // fires mid-import and replaces the results with an empty query's.
            searchField.text = "";
            searchTimer.stop();

            root.model.importPhoneContacts();
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
        ToolIcon {
            platformIconId: "toolbar-view-menu"
            onClicked: (pageMenu.status === DialogStatus.Closed) ? pageMenu.open() : pageMenu.close()
        }
    }

    // This page exists to be typed into, so unlike the pull-down bar in ChatListView it
    // takes focus straight away.
    Component.onCompleted: searchField.forceActiveFocus()
}
