import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Item {
    id: root

    property variant model

    anchors.fill: parent

    ListView {
        id: listView
        anchors.fill: parent
        // Rows in the cache buffer sit above the viewport, and unclipped they painted
        // straight over the TopBar every frame of a scroll. Here rather than at each
        // call site: two of the three set it on the ChatListView and the archived page
        // did not, so the same list clipped or not depending on how it was reached.
        clip: true
        // height * 2 kept roughly 36 extra ChatItem delegates alive off-screen, each
        // holding a decoded avatar. Half a screen either side is plenty of runway for
        // flicking and costs a fraction of the memory.
        cacheBuffer: listView.height / 2
        delegate: ChatItem {}
        model: root.model
        snapMode: ListView.SnapToItem

        // The search field sits above the first row rather than in a bar of its own:
        // pull the list down to reach it, and it costs no screen space the rest of
        // the time - which on a 854px screen already carrying a TopBar and a tab row
        // is the whole argument.
        header: Item {
            id: searchHeader

            width: listView.width
            height: 72

            TextField {
                id: searchField

                anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 8; bottomMargin: 8 }
                placeholderText: qsTr("Search")
                platformStyle: TextFieldStyle { paddingRight: clearIcon.width + 16 }
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                // Each keystroke re-scans the model, so coalesce a burst of them the
                // way ChatModel's own sort timer does.
                onTextChanged: filterTimer.restart()

                Image {
                    id: clearIcon

                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    source: "image://theme/icon-m-input-clear"
                    visible: searchField.text.length > 0

                    MouseArea {
                        // The icon is smaller than a fingertip; the margins are the
                        // touch target, not the artwork.
                        anchors { fill: parent; margins: -12 }
                        onClicked: {
                            searchField.text = "";
                            searchField.closeSoftwareInputPanel();
                        }
                    }
                }
            }

            Timer {
                id: filterTimer

                interval: 300
                onTriggered: {
                    root.model.filter = searchField.text;

                    // Setting the filter resets the model, which puts the view back at
                    // the top - i.e. on the header. That is right while searching and
                    // wrong once the field is empty again.
                    if (searchField.text === "")
                        listView.hideSearch();
                }
            }
        }

        // ListView.positionViewAtBeginning() includes the header, so it would show the
        // search field. Putting row 0 at the top is what tucks it away.
        function hideSearch() {
            if (count > 0)
                positionViewAtIndex(0, ListView.Beginning);
        }

        // The list is empty when it is built, so the header can only be tucked away
        // once the first chats arrive.
        property bool searchTucked: false

        onCountChanged: {
            if (!searchTucked && count > 0) {
                searchTucked = true;
                hideSearch();
            }
        }

        // The model shows everything it has, so reaching the bottom is the signal to
        // pull the next page from TDLib. QML1's ListView has no fetchMore of its own.
        onAtYEndChanged: {
            if (atYEnd)
                root.model.loadMore()
        }
    }

    // The chat the action menu is acting on. One menu for the whole list - declaring
    // a ContextMenu inside the delegate would build one popup per visible row.
    QtObject {
        id: menuTarget

        property variant chatId: 0
        property string title: ""
        property bool isPinned: false
        property bool isMuted: false
        property bool isUnread: false

        function open(id, chatTitle, pinned, muted, unread) {
            chatId = id;
            title = chatTitle;
            isPinned = pinned;
            isMuted = muted;
            isUnread = unread;
            chatMenu.open();
        }
    }

    ContextMenu {
        id: chatMenu

        MenuLayout {
            MenuItem {
                text: menuTarget.isPinned ? qsTr("UnpinFromTop") : qsTr("PinToTop")
                onClicked: root.model.toggleChatIsPinned(menuTarget.chatId, !menuTarget.isPinned)
            }

            MenuItem {
                text: menuTarget.isUnread ? qsTr("MarkAsRead") : qsTr("MarkAsUnread")
                onClicked: {
                    if (menuTarget.isUnread)
                        root.model.markChatAsRead(menuTarget.chatId)
                    else
                        root.model.markChatAsUnread(menuTarget.chatId)
                }
            }

            MenuItem {
                text: menuTarget.isMuted ? qsTr("UnmuteNotifications") : qsTr("MuteNotifications")
                onClicked: root.model.setChatMuted(menuTarget.chatId, !menuTarget.isMuted)
            }

            MenuItem {
                text: qsTr("Delete")
                onClicked: deleteDialog.open()
            }
        }
    }

    QueryDialog {
        id: deleteDialog

        titleText: qsTr("DeleteChat")
        // Groups are left rather than deleted, which the model decides from the chat
        // type. Naming the chat is what makes the confirmation useful either way.
        message: menuTarget.title
        acceptButtonText: qsTr("OK")
        rejectButtonText: qsTr("Cancel")

        onAccepted: root.model.deleteChat(menuTarget.chatId)
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: visible
        visible: model.loading
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    // Otherwise a search that matches nothing is a blank screen with a text box on it,
    // which reads as broken rather than as empty.
    Label {
        anchors.centerIn: parent
        color: "#505050"
        text: qsTr("NoResult")
        visible: model.filter !== "" && model.count === 0 && !model.loading
    }

    ScrollDecorator {
        flickableItem: listView
    }

    // The populate timer and the loadingChanged->populate() hop that used to live
    // here are gone: ChatModel now seeds itself when its first batch of chats
    // arrives, and takes later chats incrementally. Driving populate() from QML as
    // well would reset the model a second time and throw away the scroll position.

    Component.onCompleted: { model.refresh() }
}
