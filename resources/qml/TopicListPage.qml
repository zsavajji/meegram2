import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

// The topics of a forum supergroup. A forum opens into this instead of straight into a
// ChatPage; picking a topic pushes an ordinary ChatPage whose message model is scoped to
// it - see ChatManager::pushTopic.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    // This page's own context, handed over at push time - its chat, its formatter and its
    // topic model. Same arrangement as ChatPage's: a page reads no shared state.
    property variant chatContext

    property variant chat: chatContext.chat
    property variant topicModel: chatContext.topicModel

    TopBar {
        id: header
        title: chat ? chat.title : ""
    }

    Item {
        anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

        ListView {
            id: listView

            anchors.fill: parent
            // Rows in the cache buffer sit above the viewport and would otherwise paint
            // over the TopBar every frame of a scroll.
            clip: true
            cacheBuffer: listView.height / 2
            model: root.topicModel
            snapMode: ListView.SnapToItem

            // One painted item per topic, laid out in C++ - see TopicRow.cpp. It sets its
            // own height.
            delegate: TopicRow {
                width: ListView.view.width
                listModel: root.topicModel
                row: index
                inverted: theme.inverted
                actions: rowActions
            }

            // The model shows everything it has, so reaching the bottom is the signal to
            // ask for the next page. QML1's ListView has no fetchMore of its own.
            onAtYEndChanged: {
                if (atYEnd)
                    root.topicModel.loadMore()
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: visible
            visible: root.topicModel.loading && root.topicModel.count === 0
            platformStyle: BusyIndicatorStyle { size: "large" }
        }

        // A forum with no topics at all is a blank screen otherwise, which reads as
        // broken rather than as empty.
        Label {
            anchors.centerIn: parent
            color: appWindow.secondaryColor
            text: qsTr("NoResult")
            visible: root.topicModel.count === 0 && !root.topicModel.loading
        }

        ScrollDecorator {
            flickableItem: listView
        }
    }

    // What a TopicRow's tap does. The rows call these by name, with every argument - see
    // TopicRow.hpp for the list.
    QtObject {
        id: rowActions

        function openTopic(topicId, name) {
            appWindow.openTopic(root.chatContext.chatId, topicId, name);
        }
    }

    // Coming back from a topic, the previews and the unread counts this list was built
    // with are stale: it deliberately does not follow live messages (see the note in
    // ForumTopicModel's constructor), so returning to it is where it catches up.
    onStatusChanged: {
        if (status === PageStatus.Active && topicModel.count > 0)
            topicModel.refresh()
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }

    // Retires this page's context, which disposes of its topic model. Same pairing and
    // the same shutdown guard as ChatPage's.
    Component.onDestruction: {
        if (chatManager && chatContext)
            chatManager.popContext(chatContext.token)
    }
}
