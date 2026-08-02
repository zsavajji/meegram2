import QtQuick 1.1
import com.nokia.meego 1.1
import "components"

// A name and the people to put in it. The list is the same SearchModel the new-chat page
// uses, in its contacts-only mode - a public channel found by username is not something you
// can add to a group.
Page {
    id: root

    orientationLock: PageOrientation.LockPortrait

    property variant model: appWindow.chatManager.searchModel

    TopBar {
        id: header
        title: appWindow.tr("NewGroup")
    }

    TextField {
        id: titleField

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            margins: 12
        }

        placeholderText: "Group name"
        maximumLength: 128
    }

    TextField {
        id: searchField

        anchors {
            top: titleField.bottom
            left: parent.left
            right: parent.right
            leftMargin: 12
            rightMargin: 12
            topMargin: 8
        }

        placeholderText: qsTr("Search")
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

        // Contacts are matched server side like any other query, so coalesce a burst of
        // keystrokes the way the new-chat page does.
        onTextChanged: searchTimer.restart()
    }

    Timer {
        id: searchTimer

        interval: 600
        onTriggered: root.model.searchContacts(searchField.text)
    }

    ListView {
        id: listView

        anchors {
            top: searchField.bottom
            topMargin: 12
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: root.model

        delegate: ListItem {
            // The tick lives in the model, so it survives this delegate being rebuilt when
            // the query changes - which is exactly what picking several people involves.
            isSelected: model.selected

            Image {
                id: profilePhotoImage

                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                width: 64
                height: 64

                sourceSize.width: width
                sourceSize.height: height
                asynchronous: true
                fillMode: Image.PreserveAspectCrop

                source: model.photo && model.photo.isDownloadingCompleted ?
                            "image://chatPhoto/" + model.photo.localPath :
                            "image://theme/icon-l-content-avatar-placeholder"
            }

            Column {
                anchors {
                    left: profilePhotoImage.right
                    leftMargin: 16
                    right: tick.left
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    font.pixelSize: 26
                    font.bold: true
                    elide: Text.ElideRight
                    text: model.title
                }

                Label {
                    width: parent.width
                    font.pixelSize: 20
                    font.family: "Nokia Pure Light"
                    elide: Text.ElideRight
                    text: model.username
                    visible: text !== ""
                }
            }

            Image {
                id: tick

                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                // The same pair LanguageSettingsPage uses, which are known to exist in the
                // Harmattan theme.
                source: model.selected
                    ? "image://theme/meegotouch-button-radiobutton-background-selected"
                    : "image://theme/meegotouch-button-checkbox-background"
            }

            onClicked: root.model.toggleSelection(model.id)
        }
    }

    ScrollDecorator {
        flickableItem: listView
    }

    BusyIndicator {
        anchors.centerIn: listView
        running: visible
        visible: root.model.loading
        platformStyle: BusyIndicatorStyle { size: "large" }
    }

    Label {
        anchors.centerIn: listView
        color: "#505050"
        visible: root.model.searched && !root.model.loading && root.model.count === 0
        text: qsTr("NoResult")
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }

        Button {
            // A group needs a name and somebody in it; TDLib rejects either one missing,
            // and a disabled button says so before the round trip does.
            text: qsTr("Create")
            enabled: titleField.text !== "" && root.model.selectedCount > 0
            onClicked: {
                searchTimer.stop();

                // Popped before the answer comes back: ChatManager reports the new group on
                // chatAvailable, which main.qml turns into a pushed ChatPage, and coming
                // Back from it should reach the chat list rather than this page again.
                //
                // ponytail: optimistic. A creation that fails leaves the banner and no way
                // back to the typed name; keep the page until the answer lands if that
                // turns out to matter.
                appWindow.pageStack.pop();
                appWindow.chatManager.createGroup(titleField.text, root.model.selectedIds());
            }
        }
    }

    Component.onCompleted: {
        // One model for the session, so it arrives holding whatever the new-chat page last
        // searched for. Both halves have to be reset: the ticks, and the rows themselves.
        root.model.clearSelection();
        root.model.searchContacts("");

        titleField.forceActiveFocus();
    }
}
