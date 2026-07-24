import QtQuick 1.1
import com.nokia.meego 1.1
import com.nokia.extras 1.1
import "UIConstants.js" as UI

Item {
    id: root

    signal pressAndHold

    height: 88
    width: parent.width

    // Harmattan list-item "down" state: a flat background fill while pressed.
    // Declared first so it paints behind the row content. A hidden Rectangle costs
    // nothing to render, so this stays a single item rather than a Loader.
    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_BACKGROUND
        visible: mouseArea.pressed
    }

    MaskedItem {
        id: maskedItem
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 64
        height: 64
        mask: Image {
            sourceSize.width: maskedItem.width
            sourceSize.height: maskedItem.height
            source: "qrc:/images/avatar-image-mask.png"
        }
        Image {
            id: profilePhotoImage
            anchors.fill: parent
            // sourceSize is what makes the provider decode at 64x64 instead of at the
            // avatar's full resolution. asynchronous keeps that decode off the frame.
            sourceSize.width: maskedItem.width
            sourceSize.height: maskedItem.height
            asynchronous: true
            smooth: true
            fillMode: Image.PreserveAspectCrop
            source: model.photo && model.photo.localPath !== "" ?
                        "image://chatPhoto/" + model.photo.localPath :
                        "image://theme/icon-l-content-avatar-placeholder"
        }
    }

    Item {
        id: row1
        width: parent.width - maskedItem.width - 44
        height: 45
        anchors.left: maskedItem.right
        anchors.leftMargin: 16
        anchors.rightMargin: 16

        Label {
            id: title
            width: parent.width - date.width
            anchors.verticalCenter: parent.verticalCenter
            font.bold: true
            font.pixelSize: 26
            // Text colour is constant across states: the Harmattan spec signals
            // "down" with the background fill alone.
            color: "#282828"
            elide: Text.ElideRight
            text: utils.replaceEmoji(model.title)
        }

        Label {
            id: date
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            font.weight: Font.Light
            font.pixelSize: 20
            color: "#505050"
            text: model.date
        }
    }

    Item {
        height: 30
        width: parent.width - maskedItem.width - 44
        anchors.left: row1.left
        anchors.top: row1.bottom

        Label {
            id: lastMessage
            width: parent.width - mentionLoader.width - bubbleLoader.width
            anchors.verticalCenter: parent.verticalCenter
            font.weight: Font.Light
            font.pixelSize: 22
            // LastMessageRole is a plain string, so the old `model.lastMessage.isService`
            // test was always undefined and the selection-colour branch never ran.
            color: "#505050"
            elide: Text.ElideRight
            text: sanitizeText(model.lastMessage);
        }

        Loader {
            id: bubbleLoader
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            sourceComponent: model.unreadCount > 0 ? countBubble : model.isPinned ? pinnedBubble : undefined
        }

        Loader {
            id: mentionLoader
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.right: bubbleLoader.left
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: model.unreadMentionCount > 0 ? mentionBubble : undefined
        }

        Component {
            id: countBubble
            MyCountBubble {
                isMuted: model.isMuted
                value: model.unreadCount
            }
        }

        Component {
            id: pinnedBubble
            BorderImage {
                border.left: 10
                border.top: 10
                border.right: 10
                border.bottom: 10
                source: "image://theme/" + theme.colorString + "meegotouch-new-items-counter-background-combined"
                width: 32
                height: 32
                Label {
                    height: parent.height
                    color: "#ffffff"
                    font.family: icons.fontFamily
                    font.pixelSize: 32
                    anchors.horizontalCenter: parent.horizontalCenter
                    verticalAlignment: Text.AlignVCenter
                    text: icons.pinnedchat
                }
            }
        }

        Component {
            id: mentionBubble
            BorderImage {
                border.left: 10
                border.top: 10
                border.right: 10
                border.bottom: 10
                source: "image://theme/" + theme.colorString + "meegotouch-countbubble-background-large"
                width: 32
                height: 32
                Label {
                    height: parent.height
                    color: "#ffffff"
                    font.family: icons.fontFamily
                    font.pixelSize: 22
                    anchors.horizontalCenter: parent.horizontalCenter
                    verticalAlignment: Text.AlignVCenter
                    text: icons.username
                }
            }
        }
    }

    MouseArea {
        id: mouseArea

        z: 1
        anchors.fill: parent

        onClicked: {
            appWindow.openChat(model.id)
        }
        onPressAndHold: root.pressAndHold()
    }

    function sanitizeText(text) {
        return text.replace(/\n/g, " ").replace(/\r/g, " ");
    }

    Component.onCompleted: {
        var chatPhoto = model.photo;

        if (chatPhoto && chatPhoto.canBeDownloaded && !chatPhoto.isDownloadingActive && !chatPhoto.isDownloadingCompleted)
        {
            appManager.downloadFile(chatPhoto.id, 1, 0, 0, false);;
        }
    }
}
