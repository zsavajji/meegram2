import QtQuick 1.1
import com.nokia.meego 1.1

Item {
    id: root
    z: 100

    property string title
    property bool isArchived: false

    // The accent at full strength is a band of saturated blue against a near-black page,
    // which is the single biggest source of glare in the dark theme - it is the largest
    // coloured area on screen. Darkened rather than replaced, so it still follows
    // whichever accent the device theme is set to.
    property color color: isArchived ? "#424345"
                                     : theme.inverted ? Qt.darker(theme.selectionColor, 1.6)
                                                      : theme.selectionColor

    signal clicked

    anchors {
        top: parent.top
        left: parent.left
        right: parent.right
    }

    height: isPortrait ? 72 : 64

    Rectangle {
        id: background
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.color }
            GradientStop { position: 1.0; color: Qt.darker(root.color) }
        }
    }

    Label {
        id: label
        anchors {
            top: parent.top
            bottom: parent.bottom
            topMargin: isPortrait ? 16 : 12
            bottomMargin: isPortrait ? 24 : 20
            left: parent.left
            right: parent.right
            leftMargin: 16
            rightMargin: 16
        }
        height: 32

        font.family: "Nokia Pure Text Light"
        font.pixelSize: 32
        color: "#ffffff"
        elide: Text.ElideRight
        text: getTitle()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }

    function getTitle() {
        switch (appManager.connectionStateString) {
        case "Updating":
            return qsTr("Updating");
        case "Connecting":
            return qsTr("Connecting");
        case "ConnectingToProxy":
            return qsTr("ConnectingToProxy");
        case "WaitingForNetwork":
            return qsTr("WaitingForNetwork");
        default:
            return qsTr(title);
        }
    }
}
