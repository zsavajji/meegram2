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

    ScrollDecorator {
        flickableItem: listView
    }

    // The populate timer and the loadingChanged->populate() hop that used to live
    // here are gone: ChatModel now seeds itself when its first batch of chats
    // arrives, and takes later chats incrementally. Driving populate() from QML as
    // well would reset the model a second time and throw away the scroll position.

    function positionViewAtBeginning() {
        listView.positionViewAtBeginning();
    }

    Component.onCompleted: { model.refresh() }
}
