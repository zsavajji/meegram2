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

    // Video and GIF both. A GIF is a soundless mp4 as far as TDLib and the system player
    // are concerned, and MessageAnimation exposes the same properties as MessageVideo, so
    // the two bubbles differ by the word on the badge and nothing else.
    Component {
        id: videoMessageComponent

        MessageBubble {
            // Two taps, like the document delegate and unlike the photo one: a video has
            // no size ceiling and this is a metered radio, so the first tap starts the
            // download and the second hands the finished file to the system player.
            // Nothing here decodes video - an SGX530 playing H.264 inside a QML scene is
            // not a fight worth picking when Harmattan ships a player already.
            onClicked: {
                var file = model.content.file;

                if (!file)
                    return;

                if (file.isDownloadingCompleted) {
                    // False means nothing is registered for the type, same as a document.
                    if (!utils.openFile(file.localPath))
                        appWindow.showInfoBanner(qsTr("ErrorOccurred"));
                } else if (file.canBeDownloaded && !file.isDownloadingActive) {
                    appManager.downloadFile(file.id, 1, 0, 0, false);
                }
            }

            // With the filename, like a document: a video has one the sender chose, and
            // saving under TDLib's cache name would lose it.
            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.caption, model.isOutgoing,
                                            model.content.file, model.content.fileName)

            childrenWidth: videoColumn.width

            content: Column {
                id: videoColumn

                property int maxWidth: isPortrait ? 380 : 754

                width: Math.min(model.content.width > 0 ? model.content.width : maxWidth, maxWidth)
                spacing: 6

                anchors {
                    left: parent.left
                    // Computed offset, same as the photo delegate: fixed-width content
                    // cannot lean on AlignRight to sit on the outgoing side.
                    leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
                }

                Item {
                    id: videoFrame

                    width: parent.width
                    // From the metadata, so the bubble does not resize under the user when
                    // the still lands. 4:3 for a video that reports no dimensions.
                    height: model.content.width > 0 && model.content.height > 0
                                ? width * model.content.height / model.content.width
                                : width * 3 / 4

                    Rectangle {
                        anchors.fill: parent
                        color: "#30000000"
                        visible: !thumbnailImage.ready
                    }

                    Image {
                        id: thumbnailImage

                        property bool ready: model.content.thumbnailFile && model.content.thumbnailFile.isDownloadingCompleted

                        anchors.fill: parent
                        // Decode at display size; the N9 has no memory for a pixmap it
                        // would only scale down. Both dimensions, because that is what
                        // makes StickerProvider scale during the decode rather than
                        // expand the still and hand it back to be scaled down here.
                        sourceSize.width: width
                        sourceSize.height: height
                        asynchronous: true
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                        // webp goes through StickerProvider and libwebp, the same path a
                        // webp sticker takes - the provider decodes any webp by path and
                        // is only called "sticker" because stickers got there first. jpeg
                        // and png QML loads itself. Its cache was widened to 8 MB to hold
                        // stills this size; see the note on MaxCachedImageKb.
                        source: !ready
                                    ? ""
                                    : model.content.thumbnailFormat === "webp"
                                        ? "image://sticker/" + model.content.thumbnailFile.localPath
                                        : "file://" + model.content.thumbnailFile.localPath
                    }

                    Rectangle {
                        width: 80
                        height: 80
                        radius: 40
                        anchors.centerIn: parent
                        // Reads over any still, however bright.
                        color: "#80000000"

                        Label {
                            anchors.centerIn: parent
                            // Download, then play. Two states for two taps, so the first
                            // one does not look like a play button that did nothing. The
                            // spinner replaces the glyph rather than drawing over it.
                            visible: !videoIndicator.running
                            text: model.content.file && model.content.file.isDownloadingCompleted ? icons.largeplay : icons.download
                            font.family: icons.fontFamily
                            font.pixelSize: 40
                            color: "white"
                        }

                        BusyIndicator {
                            id: videoIndicator

                            anchors.centerIn: parent
                            running: model.content.file && model.content.file.isDownloadingActive
                            visible: running
                        }
                    }

                    Rectangle {
                        anchors { left: parent.left; top: parent.top; margins: 6 }
                        width: videoInfo.paintedWidth + 12
                        height: videoInfo.paintedHeight + 6
                        radius: 4
                        color: "#80000000"
                        visible: videoInfo.text !== ""

                        Label {
                            id: videoInfo

                            // Both come off the message rather than the file, so the badge
                            // reads right before anything has been downloaded. size is
                            // pre-formatted by File - td_api sizes are int53 and must not
                            // cross into QML1 as numbers. formatTime gives back nothing for
                            // a zero duration, so the separator is conditional on having
                            // two halves to separate.
                            //
                            // GIF in place of the runtime, which is how Telegram marks one
                            // and is the only thing distinguishing the two bubbles. A GIF
                            // usually reports a couple of seconds, which says nothing
                            // useful about it.
                            property string duration: model.contentType === "messageAnimation"
                                                          ? "GIF"
                                                          : utils.formatTime(model.content.duration)
                            property string size: model.content.file ? model.content.file.size : ""

                            anchors.centerIn: parent
                            text: duration !== "" && size !== "" ? duration + " - " + size : duration + size
                            color: "white"
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

            // Only the still is fetched on sight - it is a few KB, and without it the
            // bubble is an empty grey box. The video itself waits for a tap.
            Component.onCompleted: {
                var thumbnail = model.content.thumbnailFile;

                if (thumbnail && thumbnail.canBeDownloaded && !thumbnail.isDownloadingActive && !thumbnail.isDownloadingCompleted)
                    appManager.downloadFile(thumbnail.id, 1, 0, 0, false);
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
        id: voiceNoteMessageComponent

        MessageBubble {
            // The one delegate whose file is fetched on sight rather than on tap. A voice
            // note is a few KB and the whole point of it is that you press play once, not
            // once to download and again to hear it.
            Component.onCompleted: {
                var file = model.content.file;

                if (file && file.canBeDownloaded && !file.isDownloadingCompleted && !file.isDownloadingActive)
                    appManager.downloadFile(file.id, 1, 0, 0, false);
            }

            onClicked: {
                var file = model.content.file;

                if (!file || !file.isDownloadingCompleted)
                    return;

                // Same object drives every bubble, so starting one note is what stops
                // whichever was already playing.
                if (voice.playing && voice.source === file.localPath)
                    voice.stop();
                else
                    voice.play(file.localPath);
            }

            onPressAndHold: menuTarget.open(model.id, model.sender, model.content.caption, model.isOutgoing,
                                            model.content.file)

            childrenWidth: voiceColumn.width

            content: Column {
                id: voiceColumn

                property bool ready: model.content.file && model.content.file.isDownloadingCompleted
                property bool active: ready && voice.playing && voice.source === model.content.file.localPath

                width: isPortrait ? 380 : 754
                spacing: 6

                anchors {
                    left: parent.left
                    leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
                }

                Row {
                    width: parent.width
                    spacing: 12

                    Rectangle {
                        id: voiceIcon

                        width: 60
                        height: 60
                        radius: 30
                        color: model.isOutgoing ? "#40ffffff" : "#0077A8"

                        Label {
                            anchors.centerIn: parent
                            visible: !voiceIndicator.running
                            // Download, then play, then pause. Three states, because the
                            // first tap on a note that has not arrived would otherwise
                            // look like a play button that does nothing.
                            text: !voiceColumn.ready ? icons.download : voiceColumn.active ? icons.pause : icons.play
                            font.family: icons.fontFamily
                            font.pixelSize: 28
                            color: "white"
                        }

                        BusyIndicator {
                            id: voiceIndicator

                            anchors.centerIn: parent
                            running: model.content.file && model.content.file.isDownloadingActive
                            visible: running
                        }
                    }

                    Column {
                        width: parent.width - voiceIcon.width - parent.spacing
                        spacing: 2

                        Label {
                            width: parent.width
                            text: qsTr("AttachAudio")
                            color: model.isOutgoing ? "white" : "black"
                            font.pixelSize: 23
                        }

                        Label {
                            width: parent.width
                            // Duration comes off the message, not the file, so it reads
                            // right before the note has finished downloading.
                            text: utils.formatTime(model.content.duration)
                            color: model.isOutgoing ? "white" : "#505050"
                            opacity: model.isOutgoing ? 0.75 : 1.0
                            font.pixelSize: 18
                            font.weight: Font.Light
                        }
                    }
                }

                Label {
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
            case "messageVideo":
            // Through the video delegate: a GIF is a soundless mp4, so it downloads and
            // opens exactly the same way and only wants a different badge.
            case "messageAnimation":
                return videoMessageComponent;
            case "messageSticker":
                return stickerMessageComponent;
            case "messageDocument":
            // Through the document delegate rather than one of its own. Playing an mp3 is
            // the platform's job - openFile hands it to whatever Harmattan has registered
            // for the type - and downloading and saving is all the rest of that bubble
            // does. It renders under the sender's filename, which is what an audio file
            // has and a voice note does not.
            case "messageAudio":
                return documentMessageComponent;
            case "messageVoiceNote":
                return voiceNoteMessageComponent;
            default:
                return notSupportedMessageComponent;
            }
        }
    }
}
