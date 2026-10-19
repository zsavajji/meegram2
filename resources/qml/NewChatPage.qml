import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
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
            source: appWindow.themeIcon("icon-m-input-clear")
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
        // Same reasoning as ChatListView: these rows carry a decoded avatar each, and
        // with the default buffer of 0 a flick back up re-decodes every one of them.
        cacheBuffer: listView.height / 2
        model: root.model

        // One painted item per contact, laid out in C++ - see ContactRow.cpp. It sets its
        // own height.
        delegate: ContactRow {
            width: ListView.view.width
            listModel: root.model
            row: index
            inverted: theme.inverted
            actions: rowActions
        }
    }

    // What a ContactRow's tap does. The rows call these by name, with every argument -
    // see ContactRow.hpp for the list.
    QtObject {
        id: rowActions

        function activate(contactId) {
            // Left on the stack rather than popped: the chat is pushed on top, so Back
            // returns to these results, which is what a mistyped username needs.
            appWindow.openChat(contactId);
        }

        function download(fileId) {
            appManager.downloadFile(fileId, 1, 0, 0, false);
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
        color: appWindow.secondaryColor
        visible: root.model.searched && !root.model.loading && root.model.count === 0
        text: qsTr("NoResult")
    }

    Menu {
        id: pageMenu

        MenuLayout {
            MenuItem {
                text: qsTr("ImportContacts")
                onClicked: importDialog.open()
            }
        }
    }

    // Stated plainly, because this is not a lookup: the numbers leave the device and the
    // matches join the Telegram contact list on every client signed into this account.
    // The official apps do the same thing on first launch - they just do not ask.
    QueryDialog {
        id: importDialog

        titleText: qsTr("ImportContacts")
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
