import QtQuick 1.1
import com.nokia.meego 1.1

Item {
    height: loader.y + loader.height
    width: listView.width

    Loader {
        id: loader
        width: listView.width
        sourceComponent: model.isService ? serviceMessageComponent  : deleglateChooser.get(model.contentType)
    }

    Component {
        id: serviceMessageComponent

        ServiceMessageDelegate {}
    }

    Component {
        id: textMessageComponent

        MessageBubble {
            // ChatPage's root is also called "root", so it is shadowed in here.
            // menuTarget is uniquely named and reachable from the delegate scope.
            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.text, model.isOutgoing)

            childrenWidth: messageText.paintedWidth

            content: Label {
                id: messageText
                text: utils.replaceEmoji(model.content.formattedText)
                textFormat: Text.RichText
                color: model.isOutgoing ? "white" : "black"
                width: isPortrait ? 380 : 754
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                anchors {
                    left: parent.left
                    leftMargin: model.isOutgoing ? 80 : 20
                }
                font.pixelSize: 23
                horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }
    }

    Component {
        id: photoMessageComponent

        MessageBubble {
            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.caption, model.isOutgoing)

            childrenWidth: photoColumn.width

            content: Column {
                id: photoColumn

                property int maxWidth: isPortrait ? 380 : 754

                // The bubble measures its content by children[0], so the image and its
                // caption have to be one item. Never upscale past the source width.
                width: Math.min(model.content.width > 0 ? model.content.width : maxWidth, maxWidth)
                spacing: 6

                anchors {
                    left: parent.left
                    leftMargin: model.isOutgoing ? 80 : 20
                }

                Item {
                    id: photoFrame

                    width: parent.width
                    // Reserve the final geometry from the metadata, so the bubble does
                    // not resize under the user when the download lands.
                    height: model.content.width > 0 ? width * model.content.height / model.content.width : width

                    Rectangle {
                        anchors.fill: parent
                        // #AARRGGBB - a light scrim that reads on both bubble colours.
                        color: "#30000000"
                        visible: !photoImage.ready
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: visible
                        visible: !photoImage.ready
                    }

                    Image {
                        id: photoImage

                        property bool ready: model.content.file && model.content.file.isDownloadingCompleted

                        anchors.fill: parent
                        // Decode at display size. The N9 has no memory to spare for a
                        // full resolution pixmap it would only scale down.
                        sourceSize.width: width
                        asynchronous: true
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                        source: ready ? "file://" + model.content.file.localPath : ""
                    }
                }

                Label {
                    width: parent.width
                    visible: text !== ""
                    text: utils.replaceEmoji(model.content.caption)
                    textFormat: Text.RichText
                    color: model.isOutgoing ? "white" : "black"
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    font.pixelSize: 23
                    horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
                    onLinkActivated: Qt.openUrlExternally(link)
                }
            }

            // ponytail: downloads on sight. A delegate only exists for rows in view
            // plus the cache buffer, so this is "what you scrolled to" rather than
            // "the whole history" - but it still ignores whether the connection is
            // metered. Wire to Telegram's auto-download settings if that bites.
            Component.onCompleted: {
                var file = model.content.file;

                if (file && file.canBeDownloaded && !file.isDownloadingActive && !file.isDownloadingCompleted)
                    appManager.downloadFile(file.id, 1, 0, 0, false);
            }
        }
    }

    Component {
        id: notSupportedMessageComponent

        MessageBubble {
            // No text to copy or edit on an unsupported content type, so the menu
            // comes up with just Reply and Delete.
            onPressAndHold: menuTarget.open(model.id, model.sender, "", model.isOutgoing)

            childrenWidth: notSupportedMessage.paintedWidth

            content: Label {
                id: notSupportedMessage
                anchors {
                    left: parent.left
                    leftMargin: model.isOutgoing ? 80 : 20
                }
                width: isPortrait ? 380 : 754
                font {
                    bold: true
                    pixelSize: 23
                }
                horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
                wrapMode: Text.Wrap
                color: model.isOutgoing ? "white" : "black"
                text: "The message is not supported on MeeGram yet"
            }
        }
    }

    QtObject {
        id: deleglateChooser
        function get(contentType) {
            switch (contentType) {
            case "messageText":
                return textMessageComponent;
            case "messagePhoto":
                return photoMessageComponent;
            default:
                return notSupportedMessageComponent;
            }
        }
    }
}
