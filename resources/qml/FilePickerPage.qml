import QtQuick 1.1
import com.nokia.meego 1.1
// Ships in the Harmattan 1.2 sysroot. This version exposes only the fileName and
// filePath roles plus isFolder() - there is no size or date role, which is why rows
// show a name and nothing else.
import Qt.labs.folderlistmodel 1.0

import "components"

Page {
    id: root

    signal fileSelected(string path)

    // Folders deep below the starting point, so the "up one directory" row only shows
    // when there is somewhere to go. A counter rather than comparing folder urls: QUrl
    // normalisation makes string equality on those unreliable.
    property int depth: 0

    // ChatPage keeps this page alive between attachments, so closing it is really a hide
    // and reopening would otherwise land wherever the last browse ended. Inactive fires
    // once the pop animation is over, so the list does not visibly rewind on the way out.
    // The declarative folder binding is gone by then - the first drilldown overwrites it -
    // so setting it back by hand is the only way home.
    onStatusChanged: {
        if (status === PageStatus.Inactive) {
            root.depth = 0;
            folderModel.folder = utils.toFileUrl(utils.documentsPath());
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
                right: parent.right
                rightMargin: 16
                verticalCenter: parent.verticalCenter
            }
            color: "white"
            font.bold: true
            font.pixelSize: 26
            // The folder being browsed, so a few levels down still says where you are.
            // ElideLeft keeps the current folder visible rather than the root.
            elide: Text.ElideLeft
            text: root.depth === 0 ? qsTr("AttachDocument") : utils.toLocalFile(String(folderModel.folder))
        }
    }

    ListView {
        id: fileList

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true

        model: FolderListModel {
            id: folderModel

            folder: utils.toFileUrl(utils.documentsPath())
            showDirs: true
            // The header row below is the way up, so the model's own ".." is off - it
            // would sort into the middle of the list and read as a folder named "..".
            showDotAndDotDot: false
            sortField: FolderListModel.Name
        }

        // Scrolls with the list, which is what makes this the first row rather than a
        // second fixed bar under the title. Collapsed rather than swapped out at the top
        // level: assigning a different header component deletes the old item outright,
        // and doing that from the item's own click handler is a use-after-free.
        header: Item {
            // A header's parent is the content item, which has no width of its own in a
            // vertical list, so anything relying on parent.width would collapse.
            width: fileList.width
            height: root.depth > 0 ? upOneLevel.height : 0
            clip: true

            ListItem {
                id: upOneLevel

                width: parent.width

                Label {
                    anchors {
                        left: parent.left
                        leftMargin: 16
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    // Deliberately not a langpack key: the locale returns a key it does
                    // not know as itself, so plain English degrades to readable text.
                    text: qsTr("Up one directory")
                    font.pixelSize: 24
                }

                onClicked: {
                    root.depth--;
                    folderModel.folder = folderModel.parentFolder;
                }
            }
        }

        delegate: ListItem {
            // isFolder is a method, not a role, in this version of the model.
            property bool isFolder: folderModel.isFolder(index)

            subItemIndicator: isFolder

            Label {
                anchors {
                    left: parent.left
                    leftMargin: 16
                    right: parent.right
                    // Clear of the drilldown arrow on a folder row.
                    rightMargin: parent.subItemIndicator ? 60 : 16
                    verticalCenter: parent.verticalCenter
                }
                text: fileName
                font.pixelSize: 24
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }

            onClicked: {
                // This model hands out filePath as a file:// url, not a plain path, so
                // feeding it straight to toFileUrl wrapped it twice and produced a
                // directory that does not exist - which setFolder ignores in silence.
                // Normalising first works whichever of the two it turns out to be.
                var path = utils.toLocalFile(String(filePath));

                if (isFolder) {
                    root.depth++;
                    folderModel.folder = utils.toFileUrl(path);
                } else {
                    // A plain path, which is what sendDocument wants.
                    root.fileSelected(path);
                }
            }
        }

        ScrollDecorator { flickableItem: fileList }
    }

    tools: ToolBarLayout {
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
