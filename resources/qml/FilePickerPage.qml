import QtQuick 1.1
import com.nokia.meego 1.1
// Ships in the Harmattan 1.2 sysroot. This version exposes only the fileName and
// filePath roles plus isFolder() - there is no size or date role, which is why rows
// show a name and nothing else.
import Qt.labs.folderlistmodel 1.0

import "components"

Page {
    id: root

    // The picked files, newline-joined.
    signal picked(string paths)

    // Folders deep below the starting point, so the "up one directory" row only shows
    // when there is somewhere to go. A counter rather than comparing folder urls: QUrl
    // normalisation makes string equality on those unreliable.
    property int depth: 0

    // What has been ticked, as "\npath1\npath2\n" - see the note on the photo picker's
    // copy of this. Ticks survive browsing into another folder and back, which is the
    // whole point of keeping them here rather than in the delegates.
    property string selection: ""
    property int selectionCount: 0

    // Not a server limit here - each file is its own message - but the same number, so
    // there is one rule for how many attachments go at once.
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
            // Plain English, same reasoning as "Up one directory" below.
            appWindow.showInfoBanner(qsTr("Up to 10 files at once"));
            return;
        }

        selection = (selection === "" ? "\n" : selection) + path + "\n";
        ++selectionCount;
    }

    // ChatPage keeps this page alive between attachments, so closing it is really a hide
    // and reopening would otherwise land wherever the last browse ended. Inactive fires
    // once the pop animation is over, so the list does not visibly rewind on the way out.
    // The declarative folder binding is gone by then - the first drilldown overwrites it -
    // so setting it back by hand is the only way home.
    onStatusChanged: {
        if (status === PageStatus.Inactive) {
            root.depth = 0;
            folderModel.folder = utils.toFileUrl(utils.documentsPath());
            root.selection = "";
            root.selectionCount = 0;
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

            // This model hands out filePath as a file:// url, not a plain path, so
            // feeding it straight to toFileUrl wrapped it twice and produced a directory
            // that does not exist - which setFolder ignores in silence. Normalising here
            // works whichever of the two it turns out to be, and is also the plain path
            // sendDocument wants.
            property string path: utils.toLocalFile(String(filePath))

            subItemIndicator: isFolder
            isSelected: !isFolder && root.isPicked(path)

            Label {
                anchors {
                    left: parent.left
                    leftMargin: 16
                    right: parent.right
                    // Clear of the drilldown arrow on a folder row, and of the tick on
                    // a file row - both sit in the same 60px.
                    rightMargin: 60
                    verticalCenter: parent.verticalCenter
                }
                text: fileName
                font.pixelSize: 24
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }

            Image {
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                visible: !parent.isFolder
                // The same pair NewGroupPage uses, which are known to exist in the
                // Harmattan theme.
                source: parent.isSelected
                            ? "image://theme/meegotouch-button-radiobutton-background-selected"
                            : "image://theme/meegotouch-button-checkbox-background"
            }

            onClicked: {
                if (isFolder) {
                    root.depth++;
                    folderModel.folder = utils.toFileUrl(path);
                } else {
                    root.toggle(path);
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

        // Same as the photo picker: tapping a row ticks it, and this is what sends.
        ToolButton {
            text: root.selectionCount > 1 ? qsTr("Send") + " (" + root.selectionCount + ")" : qsTr("Send")
            enabled: root.selectionCount > 0
            onClicked: root.picked(root.selection)
        }
    }
}
