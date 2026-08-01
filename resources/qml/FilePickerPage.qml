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

    // Folders deep below the starting point, so Back knows whether to go up a level
    // or leave the page. A counter rather than comparing folder urls: QUrl
    // normalisation makes string equality on those unreliable.
    property int depth: 0

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
            // Up is the Back button's job, so there is no ".." row to tap.
            showDotAndDotDot: false
            sortField: FolderListModel.Name
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
                if (isFolder) {
                    root.depth++;
                    folderModel.folder = utils.toFileUrl(filePath);
                } else {
                    // A plain path, which is what sendDocument wants.
                    root.fileSelected(filePath);
                }
            }
        }

        ScrollDecorator { flickableItem: fileList }
    }

    tools: ToolBarLayout {
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: {
                if (root.depth === 0) {
                    pageStack.pop();
                    return;
                }

                root.depth--;
                folderModel.folder = folderModel.parentFolder;
            }
        }
    }
}
