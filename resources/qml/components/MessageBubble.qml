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

    // What the reply banner previews. Every content type keeps its wording under a
    // different name, and reading a property a QObject does not have gives undefined
    // rather than throwing - so this is the first one that exists, or nothing.
    property string replyPreview: model.content ? (model.content.text || model.content.caption || "") : ""

    // Springs the bubble back after a swipe. An explicit animation rather than a
    // Behavior: during the drag the Behavior would have to be switched off, and
    // whether drag.active is already false inside onReleased is not worth relying on.
    NumberAnimation {
        id: springBack

        target: root
        property: "x"
        to: 0
        duration: 150
        easing.type: Easing.OutQuad
    }

    BorderImage {
        id: bubble

        height: parent.height + (isOutgoing ? 0 : 2)
        // The quote block has to widen the bubble too, or a short message replying
        // to a long one would have its quote clipped. 11 = accent bar + its margin.
        width: Math.max(childrenWidth,
                        messageDate.paintedWidth + (sendStateIcon.visible ? sendStateIcon.paintedWidth + 6 : 0)
                            + (model.isOutgoing ? 0 : 28),
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

            // Swipe right to reply, the way Telegram does. The whole bubble follows the
            // finger: an axis-locked drag only takes the mouse grab once it has crossed
            // the threshold sideways, so a vertical flick still reaches the list.
            drag {
                target: root
                axis: Drag.XAxis
                minimumX: 0
                maximumX: 90
            }

            onClicked: root.clicked()
            onPressAndHold: root.pressAndHold()
            onPressed: springBack.stop()
            onReleased: {
                // Far enough to have been meant. composeState lives on ChatPage, the
                // same place the long-press menu's Reply calls into.
                if (root.x >= 56)
                    composeState.reply(model.id, model.sender, root.replyPreview)

                springBack.start()
            }
            // The list stealing the grab mid-drag leaves the bubble offset otherwise.
            onCanceled: springBack.start()
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
        // Cached in the model; see ChatItem's titleHtml.
        text: model.senderHtml
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
            // The bar hangs off the bubble's own left edge, on both sides: sizing this
            // from the quote's paintedWidth instead left a short quote floating in the
            // middle of a wide outgoing bubble. Reading bubble.x is safe - it depends on
            // paintedWidth, but nothing here feeds paintedWidth back (the labels are
            // capped by this Item's fixed width), so there is no binding loop.
            leftMargin: bubble.x + 10
            top: parent.top
            topMargin: root.stackTop
        }

        // Tapping the quote jumps to the message it points at. On the block rather than
        // on the bubble's MouseArea, which sits behind the whole bubble and would have
        // to work out whether the tap landed on the quote or on the message itself.
        MouseArea {
            anchors.fill: parent
            enabled: model.replyToMessageId !== ""
            onClicked: listView.goToMessage(model.replyToMessageId)
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

        // The date is right-aligned inside a label that spans the bubble, so the tick
        // cannot simply sit after the text - it has to come off the label's own right
        // edge, and that edge moves left to make room for it.
        width: parent.width - 100 - (sendStateIcon.visible ? sendStateIcon.paintedWidth + 6 : 0)
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

    // Delivery state, outgoing messages only: a clock while it is still on its way, a
    // tick once the server has it, green once the other side has read it. The model
    // gives an empty string for anything incoming, which is what hides this.
    Label {
        id: sendStateIcon

        visible: model.sendState !== ""
        anchors {
            left: messageDate.right
            leftMargin: 6
            baseline: messageDate.baseline
        }
        text: model.sendState === "sending" ? icons.sending
              : model.sendState === "failed" ? icons.sendingerror
              : model.sendState === "read" ? icons.check2
              : icons.check1
        font.family: icons.fontFamily
        font.pixelSize: 16
        // The bubble behind this is #15A8CA, so the sent tick takes the same washed-out
        // white the date does and read is the one state that gets a colour of its own.
        color: model.sendState === "read" ? "#7BE87B" : "white"
        opacity: model.sendState === "read" ? 1.0 : 0.75
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
