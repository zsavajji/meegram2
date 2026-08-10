import QtQuick 1.1
import com.nokia.meego 1.1
// Tracker-backed, and the same index the stock Gallery uses. If this import is
// missing on a device the whole page fails to load - ChatPage checks the component
// status and says so rather than doing nothing.
import QtMobility.gallery 1.1

Page {
    id: root

    // The picked photos, newline-joined - the same string sendPhotos takes.
    signal picked(string paths)

    property int columns: width > height ? 5 : 3

    // What has been ticked, as "\npath1\npath2\n". A string rather than a list: a JS
    // array in a variant property is exactly the QML1 conversion that lost the album
    // caption, and no path on this device contains a newline. Empty when nothing is
    // ticked, so the delegates' test is a plain substring search.
    property string selection: ""
    property int selectionCount: 0

    // The server's limit on one album; MessageModel enforces it too.
    property int maxSelection: 10

    function isPicked(path) {
        return selection.indexOf("\n" + path + "\n") !== -1;
    }

    function toggle(path) {
        if (isPicked(path)) {
            selection = selection.replace("\n" + path + "\n", "\n");
            --selectionCount;

            if (selectionCount === 0)
                selection = "";

            return;
        }

        if (selectionCount >= maxSelection) {
            // Plain English on purpose: the locale hands back a key it does not know as
            // itself, so this degrades to something readable. Same choice as the file
            // picker's "Up one directory".
            appWindow.showInfoBanner(qsTr("Up to 10 photos at once"));
            return;
        }

        selection = (selection === "" ? "\n" : selection) + path + "\n";
        ++selectionCount;
    }

    // ChatPage keeps this page alive between attachments, so what was ticked last time
    // would otherwise still be ticked. Inactive fires once the pop animation is over.
    onStatusChanged: {
        if (status === PageStatus.Inactive) {
            selection = "";
            selectionCount = 0;
        }
    }

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

            // String(url) rather than url: the role is a QML url type, and stringifying
            // it here is deterministic instead of relying on how a QUrl coerces into a
            // QString parameter.
            property string path: utils.toLocalFile(String(url))

            // Off the page's selection rather than off any state in here: a thumbnail
            // scrolled past is rebuilt from scratch when it comes back, and picking
            // several photos is exactly the case where that happens.
            property bool ticked: root.isPicked(path)

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

            // A ticked thumbnail reads as picked from across the grid, which a small
            // corner mark on its own does not.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                color: "#0077A8"
                opacity: 0.35
                visible: parent.ticked
            }

            Image {
                anchors { right: parent.right; top: parent.top; margins: 6 }
                // The same pair NewGroupPage uses, which are known to exist in the
                // Harmattan theme.
                source: parent.ticked
                            ? "image://theme/meegotouch-button-radiobutton-background-selected"
                            : "image://theme/meegotouch-button-checkbox-background"
            }

            MouseArea {
                id: mouseArea

                anchors.fill: parent
                onClicked: root.toggle(parent.path)
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

        // Tapping a thumbnail ticks it rather than sending it, so the send is a button -
        // the same shape NewGroupPage's Create has. The count only appears once there is
        // more than one, where it is the part worth checking before sending.
        ToolButton {
            text: root.selectionCount > 1 ? qsTr("Send") + " (" + root.selectionCount + ")" : qsTr("Send")
            enabled: root.selectionCount > 0
            onClicked: root.picked(root.selection)
        }
    }
}
