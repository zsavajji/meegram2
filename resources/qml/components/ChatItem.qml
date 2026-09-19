import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0

Item {
    id: root

    height: 88
    width: parent.width

    // Harmattan list-item "down" state: a flat wash while pressed. Declared first so
    // it paints behind the row content; a hidden Rectangle costs nothing to render,
    // so this stays a single item rather than a Loader.
    //
    // Same treatment ListItem.qml uses for its highlight. The first attempt used
    // UIConstants COLOR_BACKGROUND (#E0E1E2), which is so close to the list
    // background that the press was invisible.
    Rectangle {
        anchors.fill: parent
        color: "#8c8c8c"
        opacity: 0.5
        visible: mouseArea.pressed
    }

    // Was a MaskedItem wrapping this Image, plus a second Image holding the mask - so
    // every row carried three items and ran the cutout live. ChatPhotoProvider now
    // returns the avatar already cropped and masked, cached, so a plain Image does.
    Image {
        id: profilePhotoImage

        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 64
        height: 64

        // sourceSize is what makes the provider decode at 64x64 instead of at the
        // avatar's full resolution. asynchronous keeps that decode off the frame.
        sourceSize.width: width
        sourceSize.height: height
        asynchronous: true
        // The provider crops to sourceSize, so there is nothing left to scale and
        // nothing for smooth to interpolate. PreserveAspectCrop is still here for the
        // theme placeholder, which does not come through the provider.
        fillMode: Image.PreserveAspectCrop
        // isDownloadingCompleted, not localPath: TDLib fills in the local path as
        // soon as the download starts, so testing the path alone points the
        // provider at a half written file.
        source: model.photo && model.photo.isDownloadingCompleted ?
                    "image://chatPhoto/" + model.photo.localPath :
                    appWindow.avatarPlaceholder
    }

    Item {
        id: row1
        width: parent.width - profilePhotoImage.width - 44
        height: 45
        anchors.left: profilePhotoImage.right
        anchors.leftMargin: 16
        anchors.rightMargin: 16

        // Channels are read-only for everybody but their admins, so they are worth
        // telling apart at a glance. Fixed width rather than paintedWidth: the glyph is
        // one character of the icon font and binding a Label's width to what it painted
        // is how you get a loop.
        Label {
            id: typeIcon

            anchors.verticalCenter: parent.verticalCenter
            visible: model.type === Chat.Channel
            width: visible ? 30 : 0
            font.family: icons.fontFamily
            font.pixelSize: 24
            color: appWindow.secondaryColor
            text: icons.channel
        }

        Label {
            id: title
            anchors.left: typeIcon.right
            width: parent.width - typeIcon.width - date.width
            anchors.verticalCenter: parent.verticalCenter
            font.bold: true
            font.pixelSize: 26
            // No colour of its own: Label already takes the theme foreground, which
            // is what inverts it. Constant across states either way - the Harmattan
            // spec signals "down" with the background fill alone.
            elide: Text.ElideRight
            // elideEmoji, not replaceEmoji: emoji markup makes this rich text, and
            // rich text ignores the elide above. Plain titles come back untouched and
            // still elide here.
            text: utils.elideEmoji(model.title, font, width)
        }

        Label {
            id: date
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            font.weight: Font.Light
            font.pixelSize: 20
            color: appWindow.secondaryColor
            text: model.date
        }
    }

    Item {
        height: 30
        width: parent.width - profilePhotoImage.width - 44
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
            color: appWindow.secondaryColor
            elide: Text.ElideRight
            // ChatModel already strips the line breaks and caches the result, so this
            // is a plain read - and plain text, so say so rather than let AutoText
            // sniff every preview for markup.
            textFormat: Text.PlainText
            text: model.lastMessage
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

        onClicked: appWindow.openChat(model.id)
        // menuTarget lives in ChatListView.qml and resolves through the delegate's
        // context. Handled here rather than in the delegate block over there because
        // bare "model" there would sit next to ChatListView's own model property.
        onPressAndHold: menuTarget.open(model.id, model.title, model.isPinned, model.isMuted,
                                        model.unreadCount > 0 || model.isMarkedAsUnread)
    }

    Component.onCompleted: {
        var chatPhoto = model.photo;

        if (chatPhoto && chatPhoto.canBeDownloaded && !chatPhoto.isDownloadingActive && !chatPhoto.isDownloadingCompleted)
        {
            appManager.downloadFile(chatPhoto.id, 1, 0, 0, false);;
        }
    }
}
