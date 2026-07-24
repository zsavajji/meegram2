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
        // height * 2 kept roughly 36 extra ChatItem delegates alive off-screen, each
        // holding a decoded avatar. Half a screen either side is plenty of runway for
        // flicking and costs a fraction of the memory.
        cacheBuffer: listView.height / 2
        delegate: ChatItem {}
        model: root.model
        snapMode: ListView.SnapToItem
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
