import QtQuick 1.1
import com.nokia.meego 1.1

Item {
    id: root

    property alias content: contentItem.children

    property int childrenWidth

    signal clicked
    signal pressAndHold

    // Dips and comes back when the view jumps here, driven by the list's one shared
    // animation. On the whole message, not on the balloon below: the text, the sender and
    // the content are siblings of that shape rather than children of it, so dimming it
    // faded the balloon behind the words and left the words alone - which on an incoming
    // one is a near-white shape on a near-white page.
    opacity: flashing ? listView.flashOpacity : 1.0

    // Vertical stack: sender, optional reply quote, content, reactions, date. Each term
    // is conditional on its part being present, so a plain message keeps exactly the
    // height it had before replies existed. reactionsRow is already zero when the message
    // has none, so it needs no test of its own.
    height: contentItem.children[0].height
          // The flat layout spends no line on the date: it shares the top row, which
          // hasTopRow has already paid for.
          + (appWindow.showBubbles ? messageDate.height : 0)
          + (root.hasTopRow ? senderLabel.height : 0)
          + (replyBlock.visible ? replyBlock.height + 6 : 0)
          + reactionsRow.height
          + (root.sided ? 30 : 28);
    width: parent.width

    // Sided right: your own messages, in both layouts - the flat one drops the balloon,
    // not the sides. Layout only.
    property bool sided: model.isOutgoing

    // ...and whether that side is the accent balloon, which is the separate question the
    // colours ask: white reads on that balloon and on nothing else. See
    // appWindow.isOnBubble.
    property bool onBubble: appWindow.isOnBubble(model.isOutgoing)

    // Read once here because the pills below live inside a Repeater, where `model` is the
    // Repeater's own and the row's roles are out of reach - the same shadowing the album
    // delegate documents. The id goes as a string rather than through the number role,
    // which is the trip Common.hpp does not trust.
    property string messageId: model.idString
    // Empty for the messages nobody reacted to, which is nearly all of them - the model
    // answers that case without building anything, so a screenful costs a null check each.
    property variant reactionList: model.reactions

    // Whether anything occupies the row above the content. In bubble mode that is the
    // sender name, when there is one. In the flat layout the timestamp lives there too,
    // so the row is always there even on a continuation that shows no name.
    property bool hasTopRow: appWindow.showBubbles ? senderLabel.visible : true

    // Where that row ends and everything below it begins.
    property int stackTop: root.hasTopRow ? 46 : 16

    // How much of the top row the timestamp and its tick are using. Zero in bubble mode,
    // where the date is on the bottom line and shares the row with nothing. Only read by
    // things measuring *back* from the right edge, so it cannot feed the date's own width
    // and close a loop.
    property int timeWidth: appWindow.showBubbles
                                ? 0
                                : messageDate.paintedWidth + (sendStateIcon.visible ? sendStateIcon.paintedWidth + 6 : 0)

    // ...and where that stack starts horizontally. On the outgoing side the parts are
    // pushed right by a fixed inset; everywhere else they clear the avatar gutter.
    property int stackLeft: root.sided ? 80 : 20 + root.avatarSpace

    // An avatar in the left gutter, the way Telegram draws a group. The model decides who
    // gets one and says so with showsSender, so the bubble needs to know nothing about the
    // chat type - a private chat, a channel and your own messages get none in either
    // layout, because there is only ever one other person for it to be. The name is the
    // looser rule: see senderLabel, which the flat layout shows everywhere.
    //
    // The flat layout adds one condition, that the message *opens* a run, so five in a row
    // from one person carry one avatar between them rather than five.
    //
    // Bubble mode still draws one per message. Telegram hangs a single avatar off the
    // *last* of a run in bubbles, because that is where the balloon's tail points - the
    // opposite end from the flat layout, and the one that costs a re-flag on every
    // arrival rather than none.
    //
    // That reason left with the tail, now the balloon is a drawn shape: nothing points at
    // the avatar any more, so this could take the flat layout's rule and cost nothing.
    // Not done - it is a visible change to a layout this work was not asked to alter.
    property bool showAvatar: model.showsSender && (appWindow.showBubbles || model.opensRun)

    // Whether the gutter is reserved, which is not the same question: a continuation
    // draws no avatar and still has to clear the gutter, or the second message of a run
    // would slide left and the column would come apart. This asks about the chat rather
    // than the message, which is exactly what showsSender answers.
    property bool hasGutter: model.showsSender
    // Everything on the incoming side shifts by this, which is the whole cost of the
    // gutter - the bubble, the labels and the content all measure from parent.left.
    property int avatarSpace: hasGutter ? 52 : 0

    // Only set by a delegate whose text lives somewhere this cannot reach - an album's
    // caption is on another message of the batch. Everything else is read off the content
    // when a swipe actually commits, rather than for every bubble the list builds.
    property string replyPreview: ""

    // True while the view has just jumped here from a quote block. Two strings, both from
    // the model: comparing against model.id meant comparing a string to a qlonglong that
    // had crossed into QML1 as a number, which is not a trip this codebase trusts. The
    // empty test is what keeps it free - with no flash in flight the id is never read,
    // so a screenful of these costs one string comparison each.
    property bool flashing: listView.flashMessageId !== "" && listView.flashMessageId === model.idString

    // The two halves of the swipe, so any MouseArea covering part of the bubble - the
    // quote block, an album cell - can hand its drag to this one instead of repeating
    // the decision. commit is false when the gesture was cancelled rather than released.
    function beginSwipe() {
        springBack.stop()
    }

    function endSwipe(commit) {
        // 56px is far enough to have been meant rather than a flick that wandered. The
        // bubble travels left, so the distance is negative.
        if (commit && root.x <= -56) {
            var content = model.content;

            // Each content type keeps its wording under a different name, and reading a
            // property a QObject does not have gives undefined rather than throwing - so
            // this is the first one that exists, or nothing.
            var preview = replyPreview !== ""
                              ? replyPreview
                              : content ? (content.text || content.caption || "") : "";

            composeState.reply(model.id, model.sender, preview);
        }

        // Only if the bubble actually moved: a tap fires released before clicked, so this
        // otherwise animated x from 0 to 0 on every tap of the quote block.
        if (root.x !== 0)
            springBack.start();
    }

    // Owned by the bubble rather than shared with the list, deliberately. A shared one
    // holds its target as a plain pointer, and a tap on a quote block destroys this very
    // delegate a moment later - the jump rebuilds the rows - so the animation went on
    // writing x into freed memory. An animation that dies with what it animates cannot.
    NumberAnimation {
        id: springBack

        target: root
        property: "x"
        to: 0
        duration: 150
        easing.type: Easing.OutQuad
    }

    Rectangle {
        id: bubble

        // The rectangle *is* the balloon. The nine-slice it replaced was a balloon with
        // transparent margin around it - a tail's worth of empty pixels along the bottom
        // of the incoming asset and the top of the outgoing one - and that margin was the
        // gap between one message and the next. A solid shape fills it: incoming bubbles
        // overlapped the row below by 2px and two outgoing ones met exactly.
        //
        // So the insets are explicit now, and the same on both sides, because a rectangle
        // has no tail to leave room for. 11 and 13 put the painted edges where the
        // asset's ink used to land, which keeps the 4px under the date line and the ~13px
        // between messages that the old bubbles read with.
        height: parent.height - 13
        // The quote block has to widen the bubble too, or a short message replying
        // to a long one would have its quote clipped. 11 = accent bar + its margin.
        width: Math.max(childrenWidth,
                        messageDate.paintedWidth + (sendStateIcon.visible ? sendStateIcon.paintedWidth + 6 : 0)
                            + (root.sided ? 0 : 28),
                        // The rank sits at the bubble's right edge, so it has to be part of
                        // what decides that edge - name and rank both, plus a gap between.
                        senderLabel.paintedWidth + (senderTitle.visible ? senderTitle.paintedWidth + 16 : 0),
                        replyBlock.visible ? replySender.paintedWidth + 11 : 0,
                        replyBlock.visible ? replyText.paintedWidth + 11 : 0,
                        // A row of pills widens the bubble the same way a quote does, so a
                        // one-word message reacted to three times is not clipped. No loop:
                        // the pills are sized by their own content, never by this.
                        reactionsRow.width) + 26
        anchors {
            left: parent.left
            leftMargin: root.sided ? parent.width - width - 10 : 10 + root.avatarSpace
            top: parent.top
            topMargin: 11
        }

        // A drawn shape. The colours are the deleted assets' own, sampled from them
        // before they went, so the palette is unchanged; what went with them is the tail.
        //
        // 13px is the radius the balloon assets were drawn with, measured off them
        // before they were deleted: their corner curve reached the left edge 13 rows
        // below the top one.
        //
        // smooth for antialiased corners. On Qt 4's software raster that is a real cost
        // per paint where the nine-slice was a blit, and it is unmeasured: `frame` in a
        // -DMEEGRAM_PROFILE=ON build against the same flick is what would settle it.
        radius: 13
        // Painted only in bubble mode, and **not** hidden in the other one: the MouseArea
        // below is this message's whole input surface - tap, long-press for the action
        // menu, swipe to reply - and an invisible item receives no events in QtQuick, so
        // hiding this took all three with it. A transparent fill draws nothing and keeps
        // them. The quote block and the rank measure from its edges either way.
        color: appWindow.showBubbles ? internal.bubbleColor() : "transparent"
        // The antialiased path is only worth asking for when there is a shape to draw.
        smooth: appWindow.showBubbles

        MouseArea {
            id: mouseArea
            anchors.fill: parent

            // Swipe left to reply: the whole bubble follows the finger. Telegram drags
            // the other way, and this does not - a deliberate local choice. An axis-locked
            // drag only takes the mouse grab once it has crossed the threshold sideways,
            // so a vertical flick still reaches the list.
            drag {
                target: root
                axis: Drag.XAxis
                minimumX: -90
                maximumX: 0
            }

            onClicked: root.clicked()
            onPressAndHold: root.pressAndHold()
            onPressed: root.beginSwipe()
            onReleased: root.endSwipe(true)
            // The list stealing the grab mid-drag leaves the bubble offset otherwise,
            // and a cancelled gesture is not a reply.
            onCanceled: root.endSwipe(false)
        }
    }

    // Bottom of the bubble rather than the top: a tall message keeps its avatar next to
    // where the sender's last line is, which is how every Telegram client hangs it. Goes
    // through the same provider as the chat list, so it arrives cropped, masked and
    // cached - see ChatPhotoProvider.
    Image {
        id: avatarImage

        width: 40
        height: 40
        visible: root.showAvatar
        anchors {
            left: parent.left
            leftMargin: 6
            top: parent.top
            // Bubble mode hangs the avatar off the balloon's bottom edge, where the tail
            // used to be. The flat layout opens with the name, so it sits at the top
            // there instead. Expressed as an offset rather than by swapping which anchor
            // is set, because clearing an anchor means assigning undefined to it.
            topMargin: appWindow.showBubbles ? Math.max(0, bubble.y + bubble.height - height - 2) : 10
        }

        sourceSize.width: width
        sourceSize.height: height
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        // The placeholder is the one thing here that is not already at 40x40.
        smooth: true
        // isDownloadingCompleted, not localPath: TDLib fills the path in when the
        // download starts, so the path alone points the provider at a partial file.
        source: !root.showAvatar
                    ? ""
                    : model.senderPhoto && model.senderPhoto.isDownloadingCompleted
                        ? "image://chatPhoto/" + model.senderPhoto.localPath
                        : appWindow.avatarPlaceholder

        // Straight to the sender's profile. Outside the balloon, so this takes no grab
        // the bubble's own area wanted and needs none of the swipe handoff the pills and
        // album cells make - nothing here covers anything.
        MouseArea {
            anchors.fill: parent

            onClicked: {
                // Empty when a chat rather than a person sent it - a channel signing its
                // posts in the discussion group - and openProfile would take that empty
                // string as a username to resolve.
                var senderId = messageModel.senderUserId(root.messageId);

                if (senderId !== "")
                    chatManager.openProfile(senderId);
            }
        }

        // Same trade as the chat list: a delegate only exists for rows in view plus the
        // cache buffer, so this fetches the people you scrolled past, not the whole
        // membership. undefined on every message with no avatar, which the guard covers.
        Component.onCompleted: {
            // The role is ungated now, so asking for it in a chat that draws no avatars
            // would resolve a user and start a download for a picture nothing shows.
            if (!root.showAvatar)
                return;

            var photo = model.senderPhoto;

            if (photo && photo.canBeDownloaded && !photo.isDownloadingActive && !photo.isDownloadingCompleted)
                appManager.downloadFile(photo.id, 1, 0, 0, false);
        }
    }

    Label {
        id: senderLabel
        y: 18
        // Stops short of the timestamp rather than running under it: in the flat layout
        // the two share the top row, and an outgoing name is right-aligned into the same
        // corner the clock sits in.
        width: appWindow.showBubbles ? parent.width - 100
                                     : parent.width - root.stackLeft - 24 - root.timeWidth
        anchors {
            left: parent.left
            leftMargin: root.stackLeft
        }
        // Coloured wherever it is doing work: several people talking, which is the
        // avatar's rule, or the flat layout, where the name is what separates one run of
        // messages from the next. On a balloon it is white like everything else there.
        color: root.onBubble ? "white"
             : root.showAvatar || !appWindow.showBubbles ? model.senderColor
             : appWindow.bubbleTextColor
        // Cached in the model, so the emoji substitution runs once a row.
        text: model.senderHtml
        font.pixelSize: 20
        font.bold: true
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 1
        horizontalAlignment: root.sided ? Text.AlignRight : Text.AlignLeft
        // In the flat layout every run opens with a name, in a 1:1 chat as much as in a
        // group and on your own messages as much as anyone's: with no balloon around
        // them, the name and the timestamp are the whole of what separates one message
        // from the next. The avatar keeps its narrower rule - it answers "several people
        // are talking", which a 1:1 chat is not.
        visible: text !== "" && (appWindow.showBubbles || model.opensRun)
    }

    // The sender's rank, on the far side of the name - "admin", "owner", or whatever
    // title the group gave them. Hung off the balloon's own right edge, which is what
    // puts it opposite the name however wide the message is.
    //
    // No width and no elide on purpose: the bubble below sizes itself from this label's
    // paintedWidth, and a width bound to the bubble would close that into a loop. An
    // intrinsic width is safe here because Telegram caps a custom title at 16 characters.
    Label {
        id: senderTitle

        anchors {
            right: bubble.right
            // Clear of the timestamp in the flat layout, where the rank and the clock are
            // on the same row and the balloon whose edge this hangs off is not drawn.
            rightMargin: appWindow.showBubbles ? 13 : root.timeWidth + 8
            baseline: senderLabel.baseline
        }
        text: model.senderTitle
        visible: text !== "" && senderLabel.visible
        color: senderLabel.color
        opacity: 0.6
        font.pixelSize: 18
        font.weight: Font.Light
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

        // The same wash the reaction pills carry, so a quote reads as a thing laid on the
        // message rather than as two more lines of it. Sized from the labels' painted
        // width rather than from the block, which is deliberately wider than its text -
        // that extra width is the tap target, and a wash across all of it would look like
        // a selection. No loop: the labels are capped by the block's own fixed width, so
        // nothing here feeds back into what it measures.
        Rectangle {
            anchors {
                left: parent.left
                top: parent.top
                topMargin: -3
            }
            width: Math.max(replySender.visible ? replySender.paintedWidth : 0,
                            replyText.visible ? replyText.paintedWidth : 0) + 11 + 8
            height: parent.height + 6
            radius: 4
            color: appWindow.tintOn(root.onBubble)
        }

        // Tapping the quote jumps to the message it points at. On the block rather than
        // on the bubble's MouseArea, which sits behind the whole bubble and would have
        // to work out whether the tap landed on the quote or on the message itself.
        MouseArea {
            anchors.fill: parent
            enabled: model.replyToMessageId !== ""

            // Covering the quote would otherwise be a dead strip for the swipe. Same
            // drag as the bubble's own area, handed to the same two functions - and a
            // gesture that dragged emits no clicked(), so the two do not collide.
            drag {
                target: root
                axis: Drag.XAxis
                minimumX: -90
                maximumX: 0
            }

            onClicked: listView.goToMessage(model.replyToMessageId)
            onPressed: root.beginSwipe()
            onReleased: root.endSwipe(true)
            onCanceled: root.endSwipe(false)
        }

        Rectangle {
            id: replyBar

            width: 3
            height: parent.height
            color: root.onBubble ? "white" : appWindow.bubbleAccentColor
            opacity: root.onBubble ? 0.6 : 1.0
        }

        Label {
            id: replySender

            y: 0
            anchors { left: replyBar.right; leftMargin: 8; right: parent.right }
            text: model.replyToSender
            visible: text !== ""
            color: root.onBubble ? "white" : appWindow.bubbleAccentColor
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
            color: root.onBubble ? "white" : appWindow.bubbleSecondaryColor
            opacity: root.onBubble ? 0.75 : 1.0
            font.pixelSize: 18
            font.weight: Font.Light
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    Item {
        id: contentItem

        // Zero-width and unanchored horizontally, so every delegate's content measures
        // its own leftMargin from here. Shifting this is what moves all of them into the
        // gutter at once - and it is zero on the outgoing side, where the content offset
        // is computed from listView.width instead.
        x: root.avatarSpace
        height: contentItem.children[0].height
        anchors {
            top: parent.top
            topMargin: root.stackTop + (replyBlock.visible ? replyBlock.height + 6 : 0)
        }
    }

    // Reaction pills, between the content and the date. Collapses to nothing when the
    // message has none - it is still anchored to contentItem.bottom with no margin, so
    // the date below lands exactly where it did before reactions existed.
    Item {
        id: reactionsRow

        // The 6 is the gap above the pills, and it only exists when there are pills.
        height: reactionsRepeater.count > 0 ? pills.height + 6 : 0
        width: pills.width

        anchors {
            left: parent.left
            // Fixed-width content cannot lean on AlignRight the way the date does, so the
            // outgoing offset is computed - same as the photo delegate. 20 puts the right
            // edge exactly where the date's lands.
            leftMargin: root.sided ? parent.width - width - 20 : 20 + root.avatarSpace
            top: contentItem.bottom
        }

        Row {
            id: pills

            y: 6
            spacing: 6

            Repeater {
                id: reactionsRepeater

                // Off root, not off `model` directly: a Repeater resolving its own model
                // property against itself is the binding loop that costs nothing to avoid.
                model: root.reactionList

                // ponytail: one line of pills. A message carrying more distinct reactions
                // than fit runs past the bubble rather than wrapping - it takes six or so
                // in portrait, which no ordinary chat reaches. A Flow with a measured
                // width is the fix if it bites.
                Rectangle {
                    height: 24
                    width: pill.width + 16
                    radius: 12
                    // Yours is filled, everyone else's is a tint of the bubble it sits on.
                    color: modelData.chosen
                               ? (root.onBubble ? "white" : "#0077A8")
                               : appWindow.tintOn(root.onBubble)

                    Row {
                        id: pill

                        anchors.centerIn: parent
                        spacing: 3

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            // 16, the smaller of the two sizes replaceEmoji draws at. A
                            // pill is a footnote on the message rather than part of it, so
                            // it reads better well under the body text. Decoded straight to
                            // size: sourceSize means the 32px asset is scaled once on load
                            // rather than on every paint, the same trade the sticker and
                            // thumbnail images make.
                            width: 16
                            height: 16
                            sourceSize.width: 16
                            sourceSize.height: 16
                            visible: modelData.icon !== ""
                            source: modelData.icon !== "" ? "qrc:/emoji/" + modelData.icon : ""
                            asynchronous: true
                        }

                        Label {
                            // Only for an emoji this build ships no asset for - the
                            // character itself beats a pill with a bare number in it.
                            // A Row skips invisible children, so it costs no space.
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.icon === ""
                            text: modelData.emoji
                            font.pixelSize: 16
                            color: modelData.chosen && !root.onBubble ? "white"
                                 : root.onBubble ? "black"
                                 : appWindow.bubbleTextColor
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.count
                            // The same 16 the date and the tick use - the pill and the
                            // line under it are the one band of small text on a bubble.
                            font.pixelSize: 16
                            color: modelData.chosen
                                       ? (root.onBubble ? "#0077A8" : "white")
                                       : (root.onBubble ? "white" : appWindow.bubbleTextColor)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent

                        // A pill covers the bubble's own MouseArea, so without this it is
                        // a dead strip for swipe-to-reply. Same handoff the album cells
                        // make - the decision stays in beginSwipe/endSwipe.
                        drag {
                            target: root
                            axis: Drag.XAxis
                            minimumX: -90
                            maximumX: 0
                        }

                        onClicked: messageModel.toggleReaction(root.messageId, modelData.emoji)
                        onPressed: root.beginSwipe()
                        onReleased: root.endSwipe(true)
                        onCanceled: root.endSwipe(false)
                    }
                }
            }
        }
    }

    Label {
        id: messageDate

        // The date is right-aligned inside a label that spans the bubble, so the tick
        // cannot simply sit after the text - it has to come off the label's own right
        // edge, and that edge moves left to make room for it.
        //
        // In the flat layout it moves to the **top** row instead, level with the sender
        // name and hard against the row's right edge. With no balloon to close a message
        // off, where one *starts* is the thing that needs marking, and a timestamp in the
        // same column at the head of every message is what makes the conversation
        // scannable. It costs no height: the row is there for the name already.
        width: appWindow.showBubbles ? parent.width - 100 : parent.width - root.stackLeft - 16
        anchors {
            left: parent.left
            leftMargin: root.stackLeft
            top: appWindow.showBubbles ? reactionsRow.bottom : parent.top
            // 22 rather than the name's own 18: the date is the smaller font of the two,
            // and this sits the two optically on one line.
            topMargin: appWindow.showBubbles ? 4 : 22
        }
        text: model.date
        color: root.onBubble ? "white" : appWindow.bubbleTextColor
        font.pixelSize: 16
        font.weight: Font.Light
        // Right for everyone in the flat layout, incoming included: the timestamps only
        // read as a column if they line up, and the label spans the row either way.
        horizontalAlignment: root.sided || !appWindow.showBubbles ? Text.AlignRight : Text.AlignLeft
    }

    // Delivery state, outgoing messages only: a clock while it is still on its way, a
    // tick once the server has it, green once the other side has read it. The model
    // gives an empty string for anything incoming, which is what hides this.
    Label {
        id: sendStateIcon

        visible: model.sendState !== ""
        anchors {
            // Before the time, not after it - "done, at 13:06" rather than "13:06, done",
            // which is the order Telegram writes it in. The label it hangs off is
            // right-aligned and spans far more than its text, so this measures back from
            // its right edge by what the text actually painted. Nothing feeds the other
            // way: the label's width no longer subtracts this icon, which it had to when
            // the icon sat outside the label's right edge.
            right: messageDate.right
            rightMargin: messageDate.paintedWidth + 6
            baseline: messageDate.baseline
        }
        text: model.sendState === "sending" ? icons.sending
              : model.sendState === "failed" ? icons.sendingerror
              : model.sendState === "read" ? icons.check2
              : icons.check1
        font.family: icons.fontFamily
        font.pixelSize: 16
        // On a balloon the sent tick takes the same washed-out white the date does, and
        // read is the one state with a colour of its own. On the page neither reads -
        // white vanishes on a light theme and the green is weak on both - so the flat
        // layout uses the accent for read and the secondary grey for the rest, which is
        // what Telegram's own flat mode does.
        color: appWindow.showBubbles ? (model.sendState === "read" ? "#7BE87B" : "white")
                                     : (model.sendState === "read" ? appWindow.accentColor : appWindow.bubbleSecondaryColor)
        opacity: model.sendState === "read" || !appWindow.showBubbles ? 1.0 : 0.75
    }

    QtObject {
        id: internal

        // The bubble's fill, in the four states the assets used to carry. Sampled from
        // them: #f5f5f5 / #939393 light incoming, #0aa7cc / #06657b light outgoing, and
        // the two dark pairs tools/make_inverted_assets.py derives.
        function bubbleColor() {
            if (root.sided)
                return mouseArea.pressed ? (theme.inverted ? "#0e8aa8" : "#06657b")
                                         : (theme.inverted ? "#0a6a82" : "#0aa7cc");

            return mouseArea.pressed ? (theme.inverted ? "#3d3d3d" : "#939393")
                                     : (theme.inverted ? "#2b2b2b" : "#f5f5f5");
        }
    }
}
