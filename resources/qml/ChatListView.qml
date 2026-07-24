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
        visible: populateTimer.running || model.loading
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    Timer {
        id: populateTimer
        interval: 200
        repeat: false
        onTriggered: model.populate()
    }

    ScrollDecorator {
        flickableItem: listView
    }

    Connections {
        target: model
        onLoadingChanged: {
            if (!model.loading)
                populateTimer.restart()
        }
    }

    function positionViewAtBeginning() {
        listView.positionViewAtBeginning();
    }

    Component.onCompleted: { model.refresh() }
}
