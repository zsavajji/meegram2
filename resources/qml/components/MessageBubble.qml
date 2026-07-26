import QtQuick 1.1
import com.nokia.meego 1.1

Item {
    id: root

    property alias content: contentItem.children

    property int childrenWidth

    signal clicked
    signal pressAndHold

    // Vertical stack: sender, optional reply quote, content, date. Each term is
    // conditional on its part being present, so a plain message keeps exactly the
    // height it had before replies existed.
    height: contentItem.children[0].height
          + messageDate.height
          + (senderLabel.text !== "" ? senderLabel.height : 0)
          + (replyBlock.visible ? replyBlock.height + 6 : 0)
          + (model.isOutgoing ? 30 : 28);
    width: parent.width

    // Where the sender label ends and everything below it begins.
    property int stackTop: senderLabel.text === "" ? 16 : 46

    BorderImage {
        height: parent.height + (isOutgoing ? 0 : 2)
        // The quote block has to widen the bubble too, or a short message replying
        // to a long one would have its quote clipped. 11 = accent bar + its margin.
        width: Math.max(childrenWidth,
                        messageDate.paintedWidth + (model.isOutgoing ? 0 : 28),
                        senderLabel.paintedWidth,
                        replyBlock.visible ? replySender.paintedWidth + 11 : 0,
                        replyBlock.visible ? replyText.paintedWidth + 11 : 0) + 26
        anchors {
            left: parent.left
            leftMargin: model.isOutgoing ? parent.width - width - 10 : 10
            top: parent.top
            topMargin: model.isOutgoing ? 1 : 8
        }

        source: internal.getBubbleImage();

        border { left: 22; right: 22; bottom: 22; top: 22; }

        opacity: 1.0

        MouseArea {
            id: mouseArea
            anchors.fill: parent

            onClicked:  {
                // Dropped two debug console.log calls here; the second ran the full
                // emoji substitution over the message body on every tap purely to
                // produce a log line.
                root.clicked()
            }
            onPressAndHold: root.pressAndHold()
        }
    }

    Label {
        id: senderLabel
        y: 18
        width: parent.width -100
        anchors {
            left: parent.left
            leftMargin: isOutgoing ? 80 : 20
        }
        color: model.isOutgoing ? "white" : "black"
        text: utils.replaceEmoji(model.sender)
        font.pixelSize: 20
        font.bold: true
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 1
        horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
        visible: text !== ""
    }

    // Quote block for a reply. Plain text on purpose: the preview comes from
    // Utils::getContent, which returns unformatted text, so there is no reason to
    // pay for a second RichText document per bubble.
    Item {
        id: replyBlock

        visible: model.replyToText !== "" || model.replyToSender !== ""
        // Each line only takes space when it has something in it. A reply to a message
        // too far back to have been loaded resolves to no preview text, and without this
        // the quote block kept a blank line under the sender. Both empty and the block
        // hides itself, so the bubble reads as an ordinary message.
        height: visible ? (replySender.visible ? replySender.height : 0) + (replyText.visible ? replyText.height : 0) : 0
        width: parent.width - 100

        anchors {
            left: parent.left
            leftMargin: model.isOutgoing ? 80 : 20
            top: parent.top
            topMargin: root.stackTop
        }

        Rectangle {
            id: replyBar

            width: 3
            height: parent.height
            color: model.isOutgoing ? "white" : "#0077A8"
            opacity: model.isOutgoing ? 0.6 : 1.0
        }

        Label {
            id: replySender

            y: 0
            anchors { left: replyBar.right; leftMargin: 8; right: parent.right }
            text: model.replyToSender
            visible: text !== ""
            color: model.isOutgoing ? "white" : "#0077A8"
            font.pixelSize: 18
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Label {
            id: replyText

            y: replySender.visible ? replySender.height : 0
            anchors { left: replyBar.right; leftMargin: 8; right: parent.right }
            text: model.replyToText
            visible: text !== ""
            color: model.isOutgoing ? "white" : "#505050"
            opacity: model.isOutgoing ? 0.75 : 1.0
            font.pixelSize: 18
            font.weight: Font.Light
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    Item {
        id: contentItem

        height: contentItem.children[0].height
        anchors {
            top: parent.top
            topMargin: root.stackTop + (replyBlock.visible ? replyBlock.height + 6 : 0)
        }
    }

    Label {
        id: messageDate

        width: parent.width -100
        anchors {
            left: parent.left
            leftMargin: isOutgoing ? 80 : 20
            top: contentItem.bottom
            topMargin: 4
        }
        text: model.date
        color: model.isOutgoing ? "white" : "black"
        font.pixelSize: 16
        font.weight: Font.Light
        horizontalAlignment: model.isOutgoing ? Text.AlignRight : Text.AlignLeft
    }

    QtObject {
        id: internal

        function getBubbleImage() {
            var imageSrc = "qrc:/images/";

            imageSrc += model.isOutgoing ? "outgoing" : "incoming"
            imageSrc += mouseArea.pressed ? "-pressed" : "-normal"

            return imageSrc + ".png";
        }
    }
}
