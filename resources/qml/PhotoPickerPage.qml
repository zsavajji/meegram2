import QtQuick 1.1
import com.nokia.meego 1.1
// Tracker-backed, and the same index the stock Gallery uses. If this import is
// missing on a device the whole page fails to load - ChatPage checks the component
// status and says so rather than doing nothing.
import QtMobility.gallery 1.1

Page {
    id: root

    signal photoSelected(string path)

    property int columns: width > height ? 5 : 3

    Item {
        id: header

        width: parent.width
        height: 72

        Rectangle {
            anchors.fill: parent
            color: "#1e1e1e"
        }

        Label {
            anchors {
                left: parent.left
                leftMargin: 16
                verticalCenter: parent.verticalCenter
            }
            color: "white"
            font.bold: true
            font.pixelSize: 26
            text: qsTr("ChatGallery")
        }
    }

    GridView {
        id: grid

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true

        cellWidth: Math.floor(width / root.columns)
        cellHeight: cellWidth

        // Thumbnails are cheap to rebuild and expensive to keep: one screen of runway
        // either side, same reasoning as the chat list.
        cacheBuffer: grid.height / 2

        model: DocumentGalleryModel {
            id: galleryModel

            rootType: DocumentGallery.Image
            // Only what the delegate reads. Every extra name is another column out
            // of Tracker for every row.
            properties: ["url"]
            // lastModified rather than dateTaken: it is a base file property, so it
            // is there for images that carry no EXIF.
            sortProperties: ["-lastModified"]
            // Nothing here should change while the picker is open, and live Tracker
            // updates are not worth the wakeups.
            autoUpdate: false
        }

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Image {
                id: thumbnail

                anchors.fill: parent
                anchors.margins: 2
                // smooth: false is deliberate - on the SGX530 the smooth scale on a
                // grid of thumbnails costs more than it buys at this size.
                smooth: false
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                clip: true
                sourceSize.width: width
                sourceSize.height: height
                source: url
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                color: "#8c8c8c"
                opacity: 0.5
                visible: mouseArea.pressed
            }

            MouseArea {
                id: mouseArea

                anchors.fill: parent
                // String(url) rather than url: the role is a QML url type, and
                // stringifying it here is deterministic instead of relying on how a
                // QUrl coerces into a QString parameter.
                onClicked: root.photoSelected(utils.toLocalFile(String(url)))
            }
        }

        ScrollDecorator { flickableItem: grid }
    }

    Label {
        anchors.centerIn: parent
        visible: galleryModel.count === 0 && galleryModel.status !== DocumentGalleryModel.Active
        font.pixelSize: 24
        color: "#808080"
        text: qsTr("NoPhotos")
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: visible
        visible: galleryModel.status === DocumentGalleryModel.Active && galleryModel.count === 0
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    tools: ToolBarLayout {
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
