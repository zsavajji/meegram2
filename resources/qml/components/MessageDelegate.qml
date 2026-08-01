import QtQuick 1.1
import com.nokia.meego 1.1
// LottieAnimation, for animated stickers.
import MyComponent 1.0

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

                // Nothing but one to three emoji draws them large, the way Telegram
                // does. 0 for everything else, which is the normal body size.
                property int emojiSize: utils.emojiOnlySize(model.content.text)

                property string html: utils.replaceEmojiSized(model.content.formattedText, emojiSize)

                // A RichText Label builds and lays out a whole QTextDocument; a plain
                // one goes straight to QTextLayout. Most messages carry no entities, no
                // link and no emoji, so most bubbles were paying for a document that
                // held nothing but a run of text - and they were paying for it while the
                // list was flicking, which is exactly when there is no frame to spare.
                //
                // Rich if it has markup or an escaped entity, and also if it has any
                // whitespace HTML would have collapsed: those are the cases where the
                // two formats do not draw the same thing, and a message that renders
                // differently depending on whether it happens to contain a link would be
                // worse than a slow one. Declared above `text` so the format is set
                // before the string lands and the text is laid out once, not twice.
                textFormat: /[<&\n\r\t]|\s\s/.test(html) ? Text.RichText : Text.PlainText
                text: html
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
            // ponytail: opens the size that was downloaded for the bubble, which only
            // has to cover 480px - so 4x zoom is soft. Keeping the largest photoSize's
            // file in MessagePhoto and fetching it here is the upgrade.
            onClicked: {
                if (model.content.file && model.content.file.isDownloadingCompleted)
                    appWindow.openPhoto(model.content.file.localPath)
            }

            // originalFile, not file: file is the size picked to cover the screen, which
            // is right for the bubble and wrong to keep. It is handed over undownloaded
            // and saveOriginal fetches it on demand.
            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.caption, model.isOutgoing,
                                           model.content.originalFile)

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
                    // The text delegate gets outgoing right-alignment for free: it spans
                    // the full width and sets AlignRight, so leftMargin 80 is arbitrary.
                    // An image is only as wide as itself, so its offset has to be
                    // computed or it sits on the left while the bubble sits on the right.
                    // 20 puts the right edge exactly where the text delegate's lands.
                    leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
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
                    // Same plain-text fast path as the text bubble above.
                    property string html: utils.replaceEmoji(model.content.caption)

                    width: parent.width
                    visible: text !== ""
                    textFormat: /[<&\n\r\t]|\s\s/.test(html) ? Text.RichText : Text.PlainText
                    text: html
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
        id: animatedStickerComponent

        LottieAnimation {
            anchors.fill: parent
            source: "file://" + model.content.file.localPath
            // Telegram plays a sticker once on arrival rather than looping forever,
            // which also spares the CPU here.
            loop: 1

            onStatusChanged: {
                if (status === LottieAnimation.Ready)
                    play()
            }
        }
    }

    Component {
        id: stickerMessageComponent

        MessageBubble {
            // Nothing to copy or edit on a sticker, so the menu is Reply and Delete.
            onPressAndHold: menuTarget.open(model.id, model.sender, "", model.isOutgoing)

            childrenWidth: stickerFrame.width

            content: Item {
                id: stickerFrame

                // 512px stickers drawn at 512px would swallow the screen; Telegram
                // sizes them well under a photo.
                property int maxSize: 180

                property bool downloaded: model.content.file && model.content.file.isDownloadingCompleted

                // tgs goes through rlottie, webp through StickerProvider and libwebp.
                // webm has no decoder here and falls through to the emoji.
                property bool animated: downloaded && model.content.format === "tgs"
                property bool still: downloaded && model.content.format === "webp"

                anchors {
                    left: parent.left
                    // Same as the photo delegate: fixed-width content cannot lean on
                    // AlignRight, so the outgoing offset is computed. A 180px sticker at
                    // leftMargin 80 is what put it under the incoming bubbles.
                    leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
                }

                width: Math.min(model.content.width > 0 ? model.content.width : maxSize, maxSize)
                height: model.content.width > 0 && model.content.height > 0
                            ? width * model.content.height / model.content.width
                            : width

                Loader {
                    anchors.fill: parent
                    sourceComponent: stickerFrame.animated ? animatedStickerComponent : undefined
                }

                Image {
                    anchors.fill: parent
                    visible: stickerFrame.still
                    asynchronous: true
                    // The provider decodes to sourceSize, so nothing is left to scale.
                    smooth: false
                    sourceSize.width: width
                    sourceSize.height: height
                    source: stickerFrame.still ? "image://sticker/" + model.content.file.localPath : ""
                }

                // ponytail: webm stickers fall back to their emoji - VP9 on an SGX530
                // is not worth a decoder. Also covers a sticker still downloading.
                Label {
                    anchors.centerIn: parent
                    visible: !stickerFrame.animated && !stickerFrame.still
                    width: parent.width
                    text: utils.replaceEmoji(model.content.emoji) + " " + qsTr("AttachSticker")
                    textFormat: Text.RichText
                    color: model.isOutgoing ? "white" : "black"
                    font.pixelSize: 23
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Only fetch what can be drawn. webm has no decoder here, so downloading
            // one to show an emoji beside it would be pure cost.
            Component.onCompleted: {
                if (model.content.format !== "tgs" && model.content.format !== "webp")
                    return

                var file = model.content.file

                if (file && file.canBeDownloaded && !file.isDownloadingActive && !file.isDownloadingCompleted)
                    appManager.downloadFile(file.id, 1, 0, 0, false)
            }
        }
    }

    Component {
        id: documentMessageComponent

        MessageBubble {
            // Deliberately not fetched on sight the way a photo is: a document has no
            // size ceiling and this is a metered radio, so the first tap starts the
            // download and the next one opens it.
            onClicked: {
                var file = model.content.file;

                if (!file)
                    return;

                if (file.isDownloadingCompleted) {
                    // False means Harmattan has nothing registered for the type. Saying
                    // so beats a tap that looks like it did nothing.
                    if (!utils.openFile(file.localPath))
                        appWindow.showInfoBanner(qsTr("ErrorOccurred"));
                } else if (file.canBeDownloaded && !file.isDownloadingActive) {
                    appManager.downloadFile(file.id, 1, 0, 0, false);
                }
            }

            // The file is passed with its name: unlike a photo, it has to be saved
            // under the name the sender gave it rather than TDLib's cache name.
            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.caption, model.isOutgoing,
                                            model.content.file, model.content.fileName)

            childrenWidth: documentColumn.width

            content: Column {
                id: documentColumn

                // Full width regardless of the filename, which is how Telegram draws a
                // file row and saves measuring the text to size the bubble.
                width: isPortrait ? 380 : 754
                spacing: 6

                anchors {
                    left: parent.left
                    // Same computed offset as the photo delegate: fixed-width content
                    // cannot lean on AlignRight to sit on the outgoing side.
                    leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
                }

                Row {
                    width: parent.width
                    spacing: 12

                    Rectangle {
                        id: fileIcon

                        property bool done: model.content.file && model.content.file.isDownloadingCompleted

                        width: 60
                        height: 60
                        radius: 30
                        color: model.isOutgoing ? "#40ffffff" : "#0077A8"

                        Label {
                            anchors.centerIn: parent
                            // The spinner replaces the glyph while it runs rather than
                            // drawing on top of it.
                            visible: !downloadIndicator.running
                            text: fileIcon.done ? icons.document : icons.download
                            font.family: icons.fontFamily
                            font.pixelSize: 28
                            color: "white"
                        }

                        BusyIndicator {
                            id: downloadIndicator

                            anchors.centerIn: parent
                            running: model.content.file && model.content.file.isDownloadingActive
                            visible: running
                        }
                    }

                    Column {
                        width: parent.width - fileIcon.width - parent.spacing
                        spacing: 2

                        Label {
                            width: parent.width
                            // ElideMiddle, not ElideRight: the extension is the part
                            // that says what the file actually is.
                            elide: Text.ElideMiddle
                            maximumLineCount: 1
                            text: model.content.fileName !== "" ? model.content.fileName : qsTr("AttachDocument")
                            color: model.isOutgoing ? "white" : "black"
                            font.pixelSize: 23
                        }

                        Label {
                            width: parent.width
                            visible: text !== ""
                            // Already formatted by File::size - td_api sizes are int53
                            // and must not cross into QML1 as numbers.
                            text: model.content.file ? model.content.file.size : ""
                            color: model.isOutgoing ? "white" : "#505050"
                            opacity: model.isOutgoing ? 0.75 : 1.0
                            font.pixelSize: 18
                            font.weight: Font.Light
                        }
                    }
                }

                Label {
                    // Same plain-text fast path as the other bubbles.
                    property string html: utils.replaceEmoji(model.content.caption)

                    width: parent.width
                    visible: text !== ""
                    textFormat: /[<&\n\r\t]|\s\s/.test(html) ? Text.RichText : Text.PlainText
                    text: html
                    color: model.isOutgoing ? "white" : "black"
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    font.pixelSize: 23
                    horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
                    onLinkActivated: Qt.openUrlExternally(link)
                }
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
            // TDLib sends a lone emoji as messageAnimatedEmoji, not messageText.
            // MessageAnimatedEmoji exposes the same text/formattedText, so the text
            // bubble renders it and emojiOnlySize gives it the large size.
            case "messageAnimatedEmoji":
                return textMessageComponent;
            case "messagePhoto":
                return photoMessageComponent;
            case "messageSticker":
                return stickerMessageComponent;
            case "messageDocument":
                return documentMessageComponent;
            default:
                return notSupportedMessageComponent;
            }
        }
    }
}
