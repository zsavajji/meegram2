import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Item {
    id: root

    property variant model

    // Revealed by pulling the list past its top, hidden again by the timer below. Off by
    // default: the field costs a row of a 854px screen that already carries a TopBar and
    // a tab row, and most visits to this list are not searches.
    property bool searchVisible: false

    anchors.fill: parent

    // Deliberately a sibling of the list rather than its header. Setting model.filter
    // resets the model, and a ListView rebuilds its header item from scratch on reset -
    // which wiped the field's text and focus the instant the debounce fired, and deleted
    // the timer from inside its own onTriggered.
    Item {
        id: searchBar

        // Doubles as the pull distance that reveals it: drag the list down by as much
        // room as the bar is about to take.
        property int expandedHeight: 72

        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: root.searchVisible ? expandedHeight : 0
        clip: true
        visible: height > 0

        // A bar that snaps in and out of a list reads as a glitch, especially when the
        // timer takes it away on its own.
        Behavior on height { NumberAnimation { duration: 150 } }

        TextField {
            id: searchField

            anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 8; bottomMargin: 8 }
            placeholderText: qsTr("Search")
            platformStyle: TextFieldStyle { paddingRight: clearIcon.width + 16 }
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

            // Each keystroke re-scans the model, so coalesce a burst of them the
            // way ChatModel's own sort timer does.
            onTextChanged: filterTimer.restart()

            // Hung off the field's own visibility rather than root.searchVisible, so the
            // cleanup cannot run before the bar has finished collapsing. Nothing grabs
            // focus on the way in: a pull should not throw the keyboard up at you, and a
            // field the user never touched is exactly the one hideTimer is there to
            // retract.
            onVisibleChanged: {
                if (visible)
                    return;

                // Clearing the text restarts the debounce, so stop it after - a filter
                // left behind by a hidden field is a list quietly missing chats.
                text = "";
                filterTimer.stop();
                closeSoftwareInputPanel();

                // This handler also runs once while the item is being built, which is
                // before an initial-property model has necessarily landed.
                if (root.model)
                    root.model.filter = "";
            }

            Image {
                id: clearIcon

                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                source: "image://theme/icon-m-input-clear"
                visible: searchField.text.length > 0

                MouseArea {
                    // The icon is smaller than a fingertip; the margins are the
                    // touch target, not the artwork.
                    anchors { fill: parent; margins: -12 }
                    onClicked: searchField.text = ""
                }
            }
        }
    }

    Timer {
        id: filterTimer

        interval: 300
        onTriggered: root.model.filter = searchField.text
    }

    Timer {
        id: hideTimer

        // Pulled into view and then ignored, it should get out of the way again. A query
        // in the field or a cursor in it keeps the bar up for as long as either lasts;
        // emptying the field and dismissing the keyboard starts the countdown over, which
        // is what closing it amounts to.
        interval: 5000
        running: root.searchVisible && !searchField.activeFocus && searchField.text === ""
        onTriggered: root.searchVisible = false
    }

    ListView {
        id: listView
        anchors { top: searchBar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
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

        // Overscrolling the top is the way in. QML1's Flickable has no dragging property
        // - it arrived with QtQuick 2 - so moving-without-flicking stands in for it, and
        // keeps a fast flick that merely bounces off the top from opening the bar.
        onContentYChanged: {
            if (moving && !flicking && contentY < -searchBar.expandedHeight)
                root.searchVisible = true;
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
