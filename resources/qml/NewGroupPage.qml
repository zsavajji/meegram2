import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

// A name and the people to put in it. The list is the same SearchModel the new-chat page
// uses, in its contacts-only mode - a public channel found by username is not something you
// can add to a group.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant model: appWindow.chatManager.searchModel

    TopBar {
        id: header
        title: appWindow.tr("NewGroup")
    }

    TextField {
        id: titleField

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            margins: 12
        }

        placeholderText: qsTr("GroupName")
        maximumLength: 128
    }

    TextField {
        id: searchField

        anchors {
            top: titleField.bottom
            left: parent.left
            right: parent.right
            leftMargin: 12
            rightMargin: 12
            topMargin: 8
        }

        placeholderText: qsTr("Search")
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

        // Contacts are matched server side like any other query, so coalesce a burst of
        // keystrokes the way the new-chat page does.
        onTextChanged: searchTimer.restart()
    }

    Timer {
        id: searchTimer

        interval: 600
        onTriggered: root.model.searchContacts(searchField.text)
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

        // One painted item per contact, laid out in C++ - see ContactRow.cpp. selectable
        // is what puts the tick on it; the tick's state still lives in the model, so it
        // survives the query changing, which is exactly what picking several people
        // involves.
        delegate: ContactRow {
            width: ListView.view.width
            listModel: root.model
            row: index
            inverted: theme.inverted
            selectable: true
            actions: rowActions
        }
    }

    // What a ContactRow's tap does. The rows call these by name, with every argument -
    // see ContactRow.hpp for the list. A tap means "choose this person" here and "open a
    // chat with them" on NewChatPage; the row reports it the same way either way.
    QtObject {
        id: rowActions

        function activate(contactId) {
            root.model.toggleSelection(contactId);
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

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }

        Button {
            // A group needs a name and somebody in it; TDLib rejects either one missing,
            // and a disabled button says so before the round trip does.
            text: qsTr("Create")
            enabled: titleField.text !== "" && root.model.selectedCount > 0
            onClicked: {
                searchTimer.stop();

                // Popped before the answer comes back: ChatManager reports the new group on
                // chatAvailable, which main.qml turns into a pushed ChatPage, and coming
                // Back from it should reach the chat list rather than this page again.
                //
                // ponytail: optimistic. A creation that fails leaves the banner and no way
                // back to the typed name; keep the page until the answer lands if that
                // turns out to matter.
                appWindow.pageStack.pop();
                appWindow.chatManager.createGroup(titleField.text, root.model.selectedIds());
            }
        }
    }

    Component.onCompleted: {
        // One model for the session, so it arrives holding whatever the new-chat page last
        // searched for. Both halves have to be reset: the ticks, and the rows themselves.
        root.model.clearSelection();
        root.model.searchContacts("");

        titleField.forceActiveFocus();
    }
}
