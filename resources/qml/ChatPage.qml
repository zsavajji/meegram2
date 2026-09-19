import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Page {
    id: root

    // Handed to this page when it was pushed (main.qml, openChat), and its own for as long
    // as the page lives. These three used to bind to ChatManager's one selection slot,
    // which every ChatPage alive shared: opening a chat from a profile rebound the page
    // underneath onto the new conversation, and `openedChatId` existed to stop that page
    // closing the wrong chat on its way out. The context makes both impossible - there is
    // no shared slot left to follow.
    property variant chatContext

    property variant chat: chatContext.chat
    property variant chatInfo: chatContext.info
    property variant messageModel: chatContext.messageModel

    // A channel you are only subscribed to takes no replies, so the composer goes
    // entirely - Column skips invisible children, so hiding them collapses the holder
    // and the message list takes the space back on its own.
    property bool canSend: chatInfo ? chatInfo.canSendMessages : true

    // What the composer is currently doing: a plain send, a reply, or an edit.
    // Named distinctly rather than living on the page root, because MessageBubble
    // also uses id: root and would shadow it.
    QtObject {
        id: composeState

        property variant replyId: 0
        property variant editId: 0
        property string senderName: ""
        property string preview: ""

        function reply(id, sender, text) {
            editId = 0;
            replyId = id;
            senderName = sender;
            preview = text;
        }

        function edit(id, text) {
            replyId = 0;
            editId = id;
            senderName = qsTr("Edit");
            preview = text;
            textArea.text = text;
            textArea.forceActiveFocus();
        }

        function clear() {
            replyId = 0;
            editId = 0;
            senderName = "";
            preview = "";
        }
    }

    // The message the action menu is acting on. One menu for the whole page - a
    // ContextMenu per delegate would build one popup per visible row.
    QtObject {
        id: menuTarget

        property variant messageId: 0
        property string sender: ""
        property string text: ""
        property bool isOutgoing: false

        // The File to save, or null - which is what hides the Save entry. An object
        // rather than a path because the original size is usually not downloaded: the
        // bubble only ever needed the smaller one. QtObject, not variant: a variant
        // initialised to null reads back as undefined.
        property QtObject saveFile: null

        // The name a document has to be saved under. Empty for a photo, whose TDLib
        // cache name is as good as any, and which goes to the gallery instead.
        property string saveName: ""

        // The photos of an album, for the entry that saves the lot. Empty for every
        // other bubble, which is what hides it.
        property variant albumPhotos
        property int albumCount: 0

        // Who sent it, for the profile entry. Resolved once here rather than in the
        // entry itself, where the visibility test and the click would each ask again.
        // Empty when a chat rather than a person sent it.
        property string senderUserId: messageModel ? messageModel.senderUserId(messageId) : ""

        function open(id, sender, text, outgoing, file, name, photos) {
            messageId = id;
            menuTarget.sender = sender;
            menuTarget.text = text;
            isOutgoing = outgoing;
            // Callers with nothing to save omit the argument entirely.
            saveFile = file || null;
            saveName = name || "";
            albumPhotos = photos;
            albumCount = photos ? photos.length : 0;
            messageMenu.open();
        }
    }

    // Saving - of one file, or of a whole album - lives on appWindow: the fullscreen
    // viewer saves too, and it is a page of its own with no way back to here.

    property bool loading: true

    property QtObject platformStyle: SheetStyle {}

    Item {
        id: content
        anchors.fill: parent
        clip: true

        Item {
            id: header
            width: parent.width
            height: headerBackground.height

            BorderImage {
                id: headerBackground
                border {
                    left: platformStyle.headerBackgroundMarginLeft
                    right: platformStyle.headerBackgroundMarginRight
                    top: platformStyle.headerBackgroundMarginTop
                    bottom: platformStyle.headerBackgroundMarginBottom
                }
                source: platformStyle.headerBackground
                width: header.width
            }

            Item {
                id: headerContent
                anchors.fill: parent

                // Paging back into history, or any later reload. The initial load is
                // the big centred one below, which owns the empty screen - showing
                // both at once would just be two spinners for one wait.
                BusyIndicator {
                    id: headerBusy

                    anchors {
                        right: parent.right
                        rightMargin: root.platformStyle.rejectButtonLeftMargin
                        verticalCenter: parent.verticalCenter
                    }
                    visible: messageModel.loading && messageModel.count > 0
                    running: visible
                    platformStyle: BusyIndicatorStyle { size: "medium" }
                }

                Row {
                    id: chatInfoRow
                    anchors.left: parent.left
                    anchors.leftMargin: root.platformStyle.rejectButtonLeftMargin
                    // Stops short of the spinner's slot whether or not it is showing,
                    // so a long title elides at a fixed width instead of jumping every
                    // time a page of history loads.
                    anchors.right: headerBusy.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 12

                    // ChatPhotoProvider returns the avatar already cropped and masked,
                    // so the MaskedItem and its mask Image that used to wrap this are
                    // gone.
                    Image {
                        id: profilePhotoImage

                        anchors.verticalCenter: parent.verticalCenter
                        height: 50
                        width: 50
                        sourceSize.width: width
                        sourceSize.height: height
                        asynchronous: true
                        // PreserveAspectCrop is for the theme placeholder; the
                        // provider's output already matches sourceSize exactly.
                        fillMode: Image.PreserveAspectCrop
                        source: chat.photo && chat.photo.isDownloadingCompleted ?
                                    "image://chatPhoto/" + chat.photo.localPath :
                                    appWindow.avatarPlaceholder

                        // The chat list delegate normally starts this, but a chat
                        // opened from a notification was never scrolled past.
                        Component.onCompleted: {
                            if (chat.photo && chat.photo.canBeDownloaded
                                    && !chat.photo.isDownloadingActive
                                    && !chat.photo.isDownloadingCompleted)
                                appManager.downloadFile(chat.photo.id, 1, 0, 0, false)
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            // elideEmoji, not replaceEmoji: emoji markup makes this rich
                            // text, and rich text ignores the elide below.
                            text: utils.elideEmoji(chatInfo.title, font, width)
                            font.bold: true
                            elide: Text.ElideRight
                            width: chatInfoRow.width - profilePhotoImage.width - chatInfoRow.spacing
                        }

                        Label {
                            text: chatInfo.status
                            font {
                                weight: Font.Light
                                pixelSize: 20
                            }
                            elide: Text.ElideRight
                            width: chatInfoRow.width - profilePhotoImage.width - chatInfoRow.spacing
                        }
                    }
                }

                // One area over the avatar and the name rather than one on each: the
                // whole row goes to the same page, and it also swallows taps that would
                // otherwise fall through to whatever is behind the header.
                MouseArea {
                    anchors.fill: chatInfoRow
                    onClicked: root.openProfile()
                }
            }
        }

        Component {
            id: sectionDateDelegate
            Item {
                width: listView.width
                height: 50

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    font.bold: true
                    font.pixelSize: 18
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: section
                    wrapMode: Text.WordWrap
                }
            }
        }

        ListView {
            id: listView
            anchors {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: inputPanelHolder.top
            }
            spacing: 6

            clip: true
            // These delegates are RichText, so each cached one carries a laid-out
            // QTextDocument. Keep a modest runway rather than three screens' worth.
            cacheBuffer: listView.height / 2

            delegate: MessageDelegate {}
            model: messageModel

            // The bubble graphic is drawn taller than its delegate - MessageBubble
            // gives the BorderImage an 8px top margin and +2 height on incoming
            // messages - so the newest one sat hard against the composer and got
            // clipped. A footer rather than anchors.bottomMargin: this only costs
            // space at the end of the list, instead of a permanent band of page
            // background between the list and the input bar.
            footer: Item {
                width: listView.width
                height: 15
            }

            highlightFollowsCurrentItem: true

            // Stay pinned to the newest message, unless the user has deliberately
            // scrolled up to read back. Updated on user-driven movement only:
            // atYEnd goes false the moment a row is appended, so binding to it
            // directly would cancel the follow on the very message it should track.
            property bool followLast: true

            onMovementEnded: {
                followLast = listView.atYEnd
                markVisibleAsRead()

                // followLast has just been updated by this movement, which is the state
                // fetchOlder needs and the atYBeginning edge never had.
                fetchOlder()
            }

            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: sectionDateDelegate

            ScrollDecorator { flickableItem: listView }

            // Positioning once is not enough: the delegates are RichText and variable
            // height, so ListView's estimate for rows it has not built yet is wrong and
            // contentHeight keeps moving underneath the offset just computed.
            //
            // Driving this from onContentHeightChanged hangs the app. Repositioning
            // builds delegates whose real heights differ from the estimate, which
            // changes contentHeight, which repositions again - and with a cacheBuffer
            // destroying and rebuilding rows either side it never converges. So it is a
            // bounded retry instead: a handful of passes and then it stops, whatever
            // the layout is doing.
            // Unread chat: centre on the last read message, so the first unread one is
            // on screen with context above it. Anything else - and any case where that
            // message is not in the loaded slice - goes to the end of the list, which is
            // the newest message: the model keeps ids sorted ascending and a message id
            // only grows. The end is never merely "the newest fetched": the model now
            // always loads the slice that ends at the chat's last message.
            function goToInitialPosition() {
                var index = messageModel.lastMessageIndex()

                if (chat.unreadCount > 0 && index >= 0 && index < listView.count)
                    listView.positionViewAtIndex(index, ListView.Center)
                else
                    listView.positionViewAtEnd()
            }

            // The message a quote block was tapped on, while it is being flashed. Read by
            // every visible bubble, which is why it is an id and not a row: a page of
            // history landing underneath renumbers the rows.
            property string flashMessageId: ""

            // What that bubble's opacity follows. Here rather than in the delegate: a
            // SequentialAnimation and its two children built per row is four objects a
            // message costs before it has drawn anything, and rows are built by the
            // dozen while the page is still sliding in. Only the flashed bubble reads it.
            property real flashOpacity: 1.0

            SequentialAnimation {
                id: flashAnimation

                // Twice: one dip of 450ms on a message you have just been carried to is
                // easy to miss, and a second one costs nothing but time.
                loops: 2

                NumberAnimation { target: listView; property: "flashOpacity"; to: 0.45; duration: 150 }
                NumberAnimation { target: listView; property: "flashOpacity"; to: 1.0; duration: 300 }

                // Ends the flash where the animation already left it, so nothing jumps -
                // and a second jump to the same message flashes it again.
                onCompleted: listView.flashMessageId = ""
            }


            // Where a tapped quote block goes. The reply may point outside the loaded
            // window, so anything not there yet is chased by jumpTimer.
            function goToMessage(messageId) {
                var index = messageModel.indexOf(messageId)

                if (index >= 0) {
                    arriveAt(index, messageId)
                    return
                }

                // Reading back is deliberate, so a message landing must not yank the
                // view to the end again - the same thing scrolling up by hand does.
                followLast = false
                jumpTimer.targetId = messageId
                jumpTimer.pagesLeft = 12
                jumpTimer.lastCount = -1
                jumpTimer.restart()
            }

            function arriveAt(index, messageId) {
                followLast = false
                // Marked now, dipped once the view has stopped moving.
                flashMessageId = messageId
                flashTimer.restart()
                // Through the settle rather than a single call: the row was very likely
                // built moments ago, and one positionViewAtIndex lands on an estimate.
                beginSettleAt(index)
            }

            Timer {
                id: flashTimer

                // Just past the settle's bounded run of five 60ms passes. Started when
                // the quote was tapped, the dip ran while the view was still jumping and
                // the row it marks had usually not been built yet. Its own timer rather
                // than a branch in the settle, so an interrupted settle still flashes.
                interval: 320
                onTriggered: flashAnimation.restart()
            }

            // Pages back the way scrolling up does, until the message turns up. Deliberately
            // not a slice centred on the target: that lands a block of ids disjoint from the
            // loaded window, and insertMessages would have to reset the model and leave a
            // hole in the middle of the history. Polling rather than hanging off
            // fetchedPosition, which is not emitted when a page comes back empty.
            //
            // ponytail: 12 pages is 240 messages at MessageSliceLimit 20, and each page is a
            // round trip. A quote pointing further back than that says so instead. The
            // centred-slice fetch is the upgrade, and it is the same work per-chat message
            // caching would need.
            Timer {
                id: jumpTimer

                property string targetId: ""
                property int pagesLeft: 0
                // What the model held when the last page was asked for. Unchanged after a
                // fetch means the chat has no more history, which ends the chase early
                // rather than spending the rest of the budget re-asking for nothing.
                property int lastCount: -1

                interval: 200
                repeat: true

                onTriggered: {
                    var index = messageModel.indexOf(targetId)

                    if (index >= 0) {
                        stop()
                        listView.arriveAt(index, targetId)
                        return
                    }

                    // A page is still in flight; loading covers both directions.
                    if (messageModel.loading)
                        return

                    if (pagesLeft <= 0 || messageModel.count === lastCount) {
                        stop()
                        appWindow.showInfoBanner(qsTr("MessageNotFound"))
                        return
                    }

                    --pagesLeft
                    lastCount = messageModel.count
                    messageModel.fetchMoreBack()
                }
            }

            Timer {
                id: settleTimer

                property int ticks: 0

                // Which position the settle is chasing: where the chat opens, or the end
                // of the list after a message landed. A message arriving needs the same
                // bounded retry the opening slice does, and for the same reason - see the
                // note above goToInitialPosition.
                property bool toEnd: false

                // Or one particular row, for a jump from a quote block. -1 when the
                // settle is chasing one of the other two.
                property int toIndex: -1

                interval: 60
                repeat: true

                onTriggered: {
                    if (toIndex >= 0)
                        listView.positionViewAtIndex(toIndex, ListView.Center)
                    else if (toEnd)
                        listView.positionViewAtEnd()
                    else
                        listView.goToInitialPosition()
                    // Also the only reliable point to report what has been read: on
                    // countChanged the delegates do not exist yet, so indexAt fails,
                    // and onMovementEnded needs the user to actually drag.
                    listView.markVisibleAsRead()

                    if (++ticks >= 5) {
                        stop()

                        listView.beginFill()
                    }
                }
            }

            function beginSettle(atEnd) {
                settleTimer.toIndex = -1
                settleTimer.toEnd = atEnd === true
                settleTimer.ticks = 0
                settleTimer.restart()
            }

            function beginSettleAt(index) {
                settleTimer.toIndex = index
                settleTimer.ticks = 0
                settleTimer.restart()
            }

            onMovementStarted: settleTimer.stop()

            // The viewport shrinking leaves contentY where it was, so the newest message
            // slides down behind the composer. Covers all three ways it shrinks: the
            // keyboard raising windowContent's heightDelta, the controls row expanding
            // with focus, and the reply banner appearing. Settling rather than a single
            // positionViewAtEnd because rows either side are rebuilt as the height
            // changes and contentHeight keeps moving - same reason a new message settles.
            onHeightChanged: {
                if (!loading && followLast)
                    beginSettle(true)
            }

            // Telling the server what has actually been seen. Without this the other
            // side never saw a message go read until it was replied to.
            function markVisibleAsRead() {
                if (listView.count === 0)
                    return

                // The row at the bottom edge of the viewport. Section headers are not
                // delegates, so indexAt can land on nothing - at the end of the list
                // the last row is the right answer anyway.
                var index = listView.indexAt(16, listView.contentY + listView.height - 4)

                // indexAt misses on a section header, and returns -1 outright before
                // the delegates exist. Showing the end of the list - including a chat
                // too short to scroll - means the newest message has been seen.
                if (index < 0 && (listView.atYEnd || listView.contentHeight <= listView.height))
                    index = listView.count - 1

                if (index >= 0)
                    messageModel.viewMessagesUpTo(index)
            }

            // The opening sequence needs both halves of "the first page is here": rows
            // present, and the model finished loading. They arrive in either order, and
            // on this path the count wins - endInsertRows() fires from inside
            // handleHistoryResponse, before cleanupFlags() clears the model's flag. So
            // onCountChanged alone always saw loading still true and skipped, leaving
            // this page's own loading stuck true for the life of the chat: the view was
            // never positioned on the newest message, and fetchOlder was blocked on the
            // first guard forever. Whichever signal lands second now does the work.
            function settleIfLoaded() {
                if (messageModel.loading || !loading || count === 0)
                    return

                goToInitialPosition()
                beginSettle()
                markVisibleAsRead()
                loading = false

                // The end of the startup timeline. Everything before it is the user
                // waiting; from here the message list is populated and positioned.
                utils.mark("messages-shown")
            }

            onCountChanged: settleIfLoaded()

            // Paging back into history. Only after the user has deliberately scrolled
            // back: while the view is still settling on the newest message a short chat
            // sits at atYBeginning too, and the prepend that answers is restored onto
            // the row that was at the top - the oldest loaded message, which is where
            // opening a chat kept landing. settleTimer.running is what excludes that,
            // rather than followLast, which cannot: it is still true from the previous
            // movement at the moment the atYBeginning edge is crossed mid-flick.
            //
            // Called from onMovementEnded as well, and that is the call that matters.
            // The edge alone deadlocks: it is rejected mid-flick for the followLast
            // above, and once the view is pinned at the top atYBeginning never changes
            // again, so it never fires a second time. Backing off and returning does not
            // re-arm it either - MessageSliceLimit is 20, so the way back down reaches
            // atYEnd and sets followLast true again (docs/profiling.md).
            function fetchOlder() {
                if (loading || settleTimer.running || followLast || !atYBeginning)
                    return

                messageModel.fetchMoreBack()
            }

            onAtYBeginningChanged: fetchOlder()

            // The other way a chat runs out of history to show: the slice that landed is
            // shorter than the screen - TDLib answers getChatHistory with fewer messages
            // than asked for while its own fetch is still in flight - so there is nothing
            // to flick. atYBeginning and atYEnd are then both stuck true, which pins
            // followLast true and blocks fetchOlder for good; the chat paged no further
            // until it was closed and reopened, by which point the slice came back full.
            //
            // Asked for by fillTimer below, which is what keeps asking until the screen
            // is covered or there is nothing older left.
            function fillViewport() {
                if (loading || settleTimer.running || count === 0 || contentHeight > height)
                    return

                messageModel.fetchMoreBack()
            }

            // Starts the retry above from scratch. Every path that might have left the
            // list shorter than the screen goes through here.
            function beginFill() {
                fillTimer.ticks = 0
                fillTimer.lastCount = -1
                fillTimer.restart()
            }

            // fetchedPosition is emitted from inside insertMessages, which runs before
            // handleHistoryResponse reaches cleanupFlags - so at that moment the model
            // still has m_backFetching set and fetchMoreBack() returns at its own guard.
            // Calling fillViewport() straight from the handler therefore did nothing at
            // all, and the chain above stopped after the single page settleTimer started.
            //
            // A deferred tick fires on a later event-loop turn, by which point the
            // response handler has run to completion and the flag is clear.
            //
            // Bounded retry rather than the single deferred call this was. Two ways the
            // chain died and left a chat showing its last message or two until it was
            // touched: a page that comes back empty - which is what TDLib answers while
            // its own fetch is still in flight - emits no fetchedPosition, and
            // fetchMoreBack does nothing at all when a page is already on its way. Neither
            // re-armed anything, and only a drag reached fetchOlder afterwards.
            Timer {
                id: fillTimer

                property int ticks: 0

                // What the list held when the last page was asked for; -1 before the first
                // ask. Unchanged after a fetch has completed means there is nothing older,
                // which is how a chat genuinely shorter than the screen stops asking.
                property int lastCount: -1

                interval: 250
                repeat: true

                onTriggered: {
                    // Enough to cover TDLib fetching the slice from the network, and no
                    // more: this must not become a poll for the life of the page.
                    if (++ticks > 12) {
                        stop()
                        return
                    }

                    // A page is on its way. Its arrival re-arms this anyway.
                    if (messageModel.loading)
                        return

                    if (listView.count > 0 && listView.contentHeight > listView.height) {
                        stop()
                        return
                    }

                    if (listView.count === lastCount) {
                        stop()
                        return
                    }

                    lastCount = listView.count
                    listView.fillViewport()
                }
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter

            width: parent.width
            height: busyIndicator.height

            // An empty list is not the same as a list still arriving. Testing count alone
            // meant a chat with no messages in it - one just created, or one that was
            // cleared - span forever, because the count it was waiting for never comes.
            // MessageModel gives up after its own retries and clears loading, which is the
            // moment this has to stop.
            visible: messageModel.loading && messageModel.count === 0

            BusyIndicator  {
                id: busyIndicator

                anchors.horizontalCenter: parent.horizontalCenter
                // Was hardcoded true, so its animation timer kept running after the
                // message list populated and the parent Column went invisible.
                running: visible
                platformStyle: BusyIndicatorStyle { size: "large" }
            }
        }

        // The other half of the same condition: nothing to show, and nothing still coming.
        Label {
            anchors.centerIn: parent

            color: appWindow.secondaryColor
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("NoMessages")
            visible: !messageModel.loading && messageModel.count === 0
        }

        Column {
            id: inputPanelHolder

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            // Mention autocomplete. Appears while an @name is being typed and there is
            // somebody to suggest; ChatManager answers with nothing for anything that is
            // not a group, so this does not have to know what kind of chat it is in.
            Rectangle {
                id: mentionPanel

                visible: root.canSend && mentionModel.count > 0
                width: parent.width
                // Three rows at most: it sits over the conversation, and a longer list
                // says less than typing one more letter does.
                height: visible ? Math.min(mentionModel.count, 3) * 64 : 0
                color: appWindow.panelColor

                ListView {
                    anchors.fill: parent
                    clip: true
                    model: ListModel { id: mentionModel }

                    delegate: ListItem {
                        width: parent.width
                        height: 64

                        // The username first - it is what gets inserted - then the name,
                        // which is what makes the row recognisable. One label rather than
                        // two: it elides as one piece of text, so a long name is what
                        // gets cut rather than the part being completed.
                        Label {
                            anchors {
                                left: parent.left
                                leftMargin: 16
                                right: parent.right
                                rightMargin: 16
                                verticalCenter: parent.verticalCenter
                            }
                            // A member with no username is offered by name alone - that
                            // is what gets inserted for them, and there is no @handle to
                            // put in front of it.
                            text: username === "" ? name
                                                  : name !== "" ? "@" + username + " - " + name : "@" + username
                            font.pixelSize: 22
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        onClicked: root.applyMention(username, name, userId)
                    }
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    opacity: 0.5
                    color: appWindow.separatorColor
                }
            }

            // Sits above the text field while a reply or an edit is pending. Collapses
            // to zero height otherwise, so it costs nothing the rest of the time.
            Rectangle {
                id: replyBanner

                visible: root.canSend && (composeState.replyId !== 0 || composeState.editId !== 0)
                width: parent.width
                height: visible ? 60 : 0
                color: appWindow.panelColor

                Rectangle {
                    id: replyBannerBar

                    anchors {
                        left: parent.left
                        leftMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    width: 3
                    height: 40
                    color: appWindow.accentColor
                }

                Label {
                    id: replyBannerSender

                    anchors {
                        left: replyBannerBar.right
                        leftMargin: 12
                        right: replyBannerClose.left
                        rightMargin: 12
                        top: replyBannerBar.top
                    }
                    text: composeState.senderName
                    color: appWindow.accentColor
                    font.pixelSize: 18
                    font.bold: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Label {
                    anchors {
                        left: replyBannerBar.right
                        leftMargin: 12
                        right: replyBannerClose.left
                        rightMargin: 12
                        top: replyBannerSender.bottom
                    }
                    text: composeState.preview
                    color: appWindow.secondaryColor
                    font.pixelSize: 18
                    font.weight: Font.Light
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Label {
                    id: replyBannerClose

                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    text: icons.close
                    font.family: icons.fontFamily
                    font.pixelSize: 32
                    color: appWindow.secondaryColor

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: {
                            // Cancelling an edit discards the prefilled draft; cancelling
                            // a reply must not touch what the user has already typed.
                            if (composeState.editId !== 0)
                                textArea.text = ""

                            composeState.clear()
                        }
                    }
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    opacity: 0.5
                    color: appWindow.separatorColor
                }
            }

            TextArea {
                id: textArea
                visible: root.canSend
                height: 64
                width: parent.width
                placeholderText: qsTr("TypeMessage")
                platformStyle: TextAreaStyle {
                    // One image for all four states: this field has no error or disabled
                    // state worth drawing differently, and the stock textedit background
                    // is a rounded box with margins that does not belong under a chat.
                    //
                    // The dark variant is the same file recoloured, from
                    // tools/make_inverted_assets.py.
                    property url field: theme.inverted ? "qrc:/images/messaging-textedit-background-inverted.png"
                                                       : "qrc:/images/messaging-textedit-background.png"

                    background: field
                    backgroundError: field
                    backgroundDisabled: field
                    backgroundSelected: field
                    backgroundCornerMargin: 1

                    // TextFieldStyle hardcodes #191919 and does not look at
                    // theme.inverted - the platform's own background graphics are light
                    // in both themes, so it never had to. Ours is not, and #191919 on
                    // #1e1e1e is text you cannot see.
                    textColor: theme.inverted ? "#ffffff" : "#191919"
                }

                // Tapping in to type raises the keyboard, which would come up over the
                // emoji panel, so the panel gets out of its way. Does not fire when the
                // panel itself lowers the keyboard: that leaves focus here untouched.
                onActiveFocusChanged: {
                    if (activeFocus)
                        emojiPanel.open = false;
                }

                onTextChanged: root.updateMentions()
                onCursorPositionChanged: root.updateMentions()
            }

            Rectangle {
                id: controls
                anchors {
                    left: parent.left
                    right: parent.right
                }
                visible: root.canSend
                height: 0
                clip: true
                color: appWindow.panelColor

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    height: 1
                    opacity: 0.5
                    color: appWindow.separatorColor
                }

                Label {
                    id: attachButton

                    anchors {
                        left: parent.left
                        leftMargin: 24
                        verticalCenter: parent.verticalCenter
                    }
                    // Doubles as discard while a note is being recorded. The attach menu
                    // is not reachable mid-recording anyway, and a recording with no way
                    // to throw it away is one you have to send by mistake.
                    text: voice.recording ? icons.close : icons.attach
                    font.family: icons.fontFamily
                    font.pixelSize: 36
                    color: attachArea.pressed ? appWindow.accentColor : appWindow.iconColor

                    MouseArea {
                        id: attachArea

                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: {
                            if (voice.recording)
                                voice.cancelRecording();
                            else
                                attachMenu.open();
                        }
                    }
                }

                Label {
                    id: recordButton

                    anchors {
                        left: attachButton.right
                        leftMargin: 36
                        verticalCenter: parent.verticalCenter
                    }
                    // Tap to start, tap to send - not hold-to-record. Holding needs the
                    // press to survive the notification banner, the lock button and every
                    // other thing this device does mid-gesture, and losing the recording
                    // to any of them is worse than one extra tap.
                    text: voice.recording ? icons.stop : icons.microphone
                    font.family: icons.fontFamily
                    font.pixelSize: 36
                    color: voice.recording ? "#d14836" : (recordArea.pressed ? appWindow.accentColor : appWindow.iconColor)

                    MouseArea {
                        id: recordArea

                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: {
                            if (voice.recording)
                                voice.stopRecording();
                            else
                                voice.startRecording();
                        }
                    }
                }

                Label {
                    anchors {
                        left: recordButton.right
                        leftMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    visible: voice.recording
                    // formatTime returns nothing for zero, and a blank label for the first
                    // second reads as a recorder that did not start.
                    text: voice.duration > 0 ? utils.formatTime(voice.duration) : "0s"
                    color: "#d14836"
                    font.pixelSize: 22
                }

                Label {
                    id: emojiButton

                    anchors {
                        right: sendButton.left
                        rightMargin: 24
                        verticalCenter: parent.verticalCenter
                    }
                    // The recording row needs the space for the elapsed time, and there
                    // is nothing to type an emoji into mid-note anyway.
                    visible: !voice.recording
                    text: emojiPanel.open ? icons.close : icons.smile
                    font.family: icons.fontFamily
                    font.pixelSize: 36
                    color: emojiPanel.open || emojiArea.pressed ? appWindow.accentColor : appWindow.iconColor

                    MouseArea {
                        id: emojiArea

                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: {
                            emojiPanel.open = !emojiPanel.open;

                            // Last on purpose: these two are the only calls here that
                            // depend on TextArea exposing the platform's input-panel
                            // methods, and a panel that opens without lowering the
                            // keyboard still beats a button that half-works.
                            if (emojiPanel.open)
                                textArea.closeSoftwareInputPanel();
                            else
                                textArea.openSoftwareInputPanel();
                        }
                    }
                }

                Button {
                    id: sendButton
                    anchors {
                        right: parent.right
                        rightMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    height: 48
                    width: 120
                    platformStyle: ButtonStyle { inverted: true }
                    text: composeState.editId !== 0 ? qsTr("Save") : qsTr("Send")
                    onClicked: {
                        // Whitespace-only counts as empty: TDLib rejects such a
                        // message, so there is nothing to gain by sending it.
                        if (textArea.text.trim() !== "") {
                            if (composeState.editId !== 0) {
                                messageModel.editMessage(composeState.editId, textArea.text)
                            } else {
                                // The pending mentions ride along: the model matches each
                                // name against the text as it goes out, so anything the
                                // user has since edited away simply finds no place to be.
                                messageModel.sendMessage(textArea.text, composeState.replyId, root.pendingMentionIds,
                                                         root.pendingMentionNames)
                                // Your own message is always worth jumping to, even
                                // from halfway up the history. It arrives back as an
                                // update, so the follow flag is what carries this.
                                listView.followLast = true
                            }

                            textArea.text = ""
                            composeState.clear()
                            root.clearPendingMentions()
                        }

                        // Outside the guard on purpose. Tapping the button moves focus
                        // off the text area, and the "open" state below is bound to
                        // textArea.activeFocus - so without this the keyboard closes
                        // and this whole panel collapses, even on an empty tap.
                        textArea.forceActiveFocus()
                    }
                }

                states: [
                    State {
                        name: "open"
                        // Held open while recording too. Bound to focus alone, tapping
                        // anywhere off the text area collapsed the panel mid-recording and
                        // took the stop button with it. Same for the emoji panel: opening
                        // it lowers the keyboard, and if that drops focus this row would
                        // collapse and take Send and the button that closes the panel away.
                        when: textArea.activeFocus || voice.recording || emojiPanel.open
                        PropertyChanges { target: controls; height: 64 }
                    }
                ]
            }

            // Takes the keyboard's place rather than covering the chat: the keyboard has
            // to come down for it either way, and down here picking several in a row does
            // not hide the message being written.
            EmojiPicker {
                id: emojiPanel

                width: parent.width

                // Appended rather than inserted at the cursor: setting .text is what
                // QML1 gives us, and that moves the cursor anyway, so put it back at
                // the end where typing resumes.
                onPicked: {
                    textArea.text += unicode;
                    textArea.cursorPosition = textArea.text.length;
                }
            }
        }
    }

    // Microphone and speaker, one per page. Shared by the composer and every bubble, so
    // starting one note stops whichever was playing without any bookkeeping.
    VoiceNote {
        id: voice

        onRecorded: {
            // Same caption-rides-along behaviour as sendPhoto, and the same follow flag:
            // your own message is worth jumping to.
            messageModel.sendVoiceNote(path, duration, textArea.text, composeState.replyId);

            textArea.text = "";
            composeState.clear();
            listView.followLast = true;
        }

        onError: appWindow.showInfoBanner(message)
    }

    // Harmattan action menu, raised by a long press on a bubble. Items that cannot
    // apply to the target hide rather than grey out, which is what the platform does.
    ContextMenu {
        id: messageMenu

        MenuLayout {
            MenuItem {
                text: qsTr("Reply")
                // Nothing to reply into when the composer is gone. Reactions stay:
                // subscribers can react to a channel post even though they cannot post.
                visible: root.canSend
                onClicked: composeState.reply(menuTarget.messageId, menuTarget.sender, menuTarget.text)
            }

            MenuItem {
                // Reacting from the menu is the way to add one that is not on the bubble
                // yet; a reaction already there is toggled by tapping its pill.
                text: qsTr("Reactions")
                onClicked: reactionMenu.open()
            }

            MenuItem {
                // Not on your own messages: your profile is a tap away under Settings,
                // and the entry is there to look somebody else up.
                text: qsTr("OpenProfile")
                visible: !menuTarget.isOutgoing && menuTarget.senderUserId !== ""
                onClicked: chatManager.openProfile(menuTarget.senderUserId)
            }

            MenuItem {
                // Who reacted, and with what. Asked of the model rather than passed in
                // through menuTarget.open, which would mean another argument on all
                // seven delegates that raise this menu.
                text: qsTr("AllReactions")
                visible: messageModel && messageModel.hasReactions(menuTarget.messageId)
                onClicked: reactionsDialog.load(menuTarget.messageId)
            }

            MenuItem {
                text: qsTr("Copy")
                visible: menuTarget.text !== ""
                onClicked: utils.copyToClipboard(menuTarget.text)
            }

            MenuItem {
                text: qsTr("Edit")
                // ponytail: outgoing text only. Telegram also refuses edits past 48h
                // and in channels without rights; TDLib rejects those and the error is
                // swallowed. Gate properly via getMessageProperties if it bites.
                visible: root.canSend && menuTarget.isOutgoing && menuTarget.text !== ""
                onClicked: composeState.edit(menuTarget.messageId, menuTarget.text)
            }

            MenuItem {
                text: qsTr("Save")
                visible: menuTarget.saveFile !== null
                onClicked: appWindow.saveOriginal(menuTarget.saveFile, menuTarget.saveName)
            }

            MenuItem {
                // The whole batch, one photo at a time. A single photo of an album is
                // saved by opening it and using the viewer's Save.
                //
                // SaveToGallery, checked against the pack on the device: SaveAllPhotos was
                // guessed and the pack has no such key, so this menu entry read
                // "SaveAllPhotos" in every language. Telegram has no "save all" string -
                // this is the label the official clients put on the same action, and the
                // entry only appears for an album, with the plain Save above it for one
                // file.
                text: qsTr("SaveToGallery")
                visible: menuTarget.albumCount > 1
                onClicked: appWindow.saveAlbum(menuTarget.albumPhotos)
            }

            MenuItem {
                text: qsTr("Delete")
                onClicked: {
                    deleteDialog.revoke = false;
                    deleteDialog.open();
                }
            }

            MenuItem {
                text: qsTr("DeleteForAll")
                // ponytail: offered on your own messages. Telegram also allows revoking
                // the other side's messages in a private chat for a while, and refuses
                // past a time limit; getMessageProperties reports can_be_deleted_for_all_users
                // properly if this turns out to be too narrow.
                visible: menuTarget.isOutgoing
                onClicked: {
                    deleteDialog.revoke = true;
                    deleteDialog.open();
                }
            }
        }
    }

    // The reaction picker. A ContextMenu rather than a Dialog because its content property
    // takes anything - this one holds a grid of emoji instead of a MenuLayout - and it
    // brings the platform's dimming, its dismissal and its rounded corners with it.
    ContextMenu {
        id: reactionMenu

        // Built once here rather than bound per cell: resolving twelve emoji to their
        // assets is a table lookup each, and the list never changes while the page lives.
        //
        // ponytail: Telegram's standard set, the same one in every chat that has not
        // restricted reactions. The correct list is per chat and comes from
        // getMessageAvailableReactions - a request, a response handler and a role, for an
        // answer that is this list nearly always. Wire it up if a restricted chat bites:
        // TDLib rejects the add and the error is swallowed, so the tap looks like it did
        // nothing.
        property variant reactions: utils.quickReactions()

        Grid {
            id: reactionGrid

            width: parent.width
            columns: 6

            Repeater {
                model: reactionMenu.reactions

                Item {
                    width: reactionGrid.width / reactionGrid.columns
                    // A full platform touch target, unlike the emoji panel's 48px cells:
                    // twelve of these fit on two rows either way, so there is no reason to
                    // make them small.
                    height: 80

                    Image {
                        anchors.centerIn: parent
                        // Native size. The assets are 32px, so drawing them larger to fill
                        // the cell would only upscale them - the touch target is the Item
                        // around this, which is full size either way.
                        width: 32
                        height: 32
                        sourceSize.width: 32
                        sourceSize.height: 32
                        source: modelData.icon !== "" ? "qrc:/emoji/" + modelData.icon : ""
                        asynchronous: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // Toggle, not add: picking the one you already gave takes it
                            // back, which is the only way to remove a reaction whose pill
                            // is off the bubble's single line.
                            messageModel.toggleReaction(menuTarget.messageId, modelData.emoji);
                            reactionMenu.close();
                        }
                    }
                }
            }
        }
    }

    // Two pickers because the two send paths are genuinely different: a photo goes
    // through the Tracker-backed gallery and is sent compressed as a photo, anything
    // else is browsed on disk and sent as a document with its type intact.
    ContextMenu {
        id: attachMenu

        MenuLayout {
            MenuItem {
                text: qsTr("AttachPhoto")
                onClicked: root.openPhotoPicker()
            }

            MenuItem {
                text: qsTr("AttachDocument")
                onClicked: root.openFilePicker()
            }
        }
    }

    QueryDialog {
        id: deleteDialog

        // Which of the two menu entries opened this. Deleting for everyone used to be
        // implied by the message being yours, so your own messages could only ever be
        // deleted for both sides.
        property bool revoke: false

        titleText: revoke ? qsTr("DeleteForAll") : qsTr("DeleteMessage")
        message: qsTr("AreYouSureDeleteSingleMessage")
        acceptButtonText: qsTr("OK")
        rejectButtonText: qsTr("Cancel")

        onAccepted: messageModel.deleteMessage(menuTarget.messageId, revoke)
    }

    // Who reacted to one message, and with what. Filled by the model's answer rather
    // than by a role: the names are not on the message, which only carries counts.
    ListModel {
        id: reactionsModel
    }

    // Opened by the answer, not by the tap - so it never flashes up empty with a spinner
    // while the request is out, and a chat that refuses the question (a big group only
    // lets its admins ask) says so in a banner instead of showing a blank list.
    Connections {
        target: messageModel

        onMessageReactionsReceived: {
            reactionsModel.clear();

            for (var i = 0; i < senders.length; ++i)
                reactionsModel.append(senders[i]);

            if (reactionsModel.count > 0)
                reactionsDialog.open();
            else
                appWindow.showInfoBanner(qsTr("NoReactions"));
        }
    }

    // SelectionDialog rather than a Dialog of its own: it is the platform's titled,
    // scrollable list, and nothing here needs buttons - a tap on a row just closes it.
    SelectionDialog {
        id: reactionsDialog

        titleText: qsTr("Reactions")
        model: reactionsModel

        function load(messageId) {
            messageModel.getMessageReactions(messageId);
        }

        delegate: Component {
            Item {
                property Style platformStyle: SelectionDialogStyle {}

                height: platformStyle.itemHeight
                anchors { left: parent.left; right: parent.right }

                MouseArea {
                    id: reactionRowArea

                    anchors.fill: parent
                    onClicked: reactionsDialog.accept()
                }

                BorderImage {
                    anchors.fill: parent
                    border { left: 22; top: 22; right: 22; bottom: 22 }
                    source: reactionRowArea.pressed ? platformStyle.itemPressedBackground : platformStyle.itemBackground
                }

                Text {
                    anchors {
                        left: parent.left
                        leftMargin: platformStyle.itemLeftMargin
                        right: reactionIcon.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    font: platformStyle.itemFont
                    color: platformStyle.itemTextColor
                    elide: Text.ElideRight
                    text: model.name
                }

                Image {
                    id: reactionIcon

                    anchors {
                        right: parent.right
                        rightMargin: platformStyle.itemRightMargin
                        verticalCenter: parent.verticalCenter
                    }
                    // Native size, like the picker's cells - the assets are 32px and
                    // drawing them larger would only upscale them.
                    width: 32
                    height: 32
                    sourceSize.width: 32
                    sourceSize.height: 32
                    asynchronous: true
                    source: model.icon !== "" ? "qrc:/emoji/" + model.icon : ""
                }

                Label {
                    // The character itself for an emoji this build ships no asset for,
                    // the same fallback the pills make.
                    anchors.centerIn: reactionIcon
                    visible: model.icon === ""
                    text: model.emoji
                    font.pixelSize: 24
                }
            }
        }
    }

    // The @name currently being typed, without its @. Empty when the cursor is not in
    // one, which is what closes the panel.
    property string mentionQuery: ""

    // Only the word the cursor is sitting in, and only when it starts a word: an address
    // inside an email is not somebody being mentioned.
    function updateMentions() {
        var head = textArea.text.substring(0, textArea.cursorPosition);
        var match = /(?:^|\s)@([A-Za-z0-9_]*)$/.exec(head);

        if (!match) {
            mentionQuery = "";
            mentionTimer.stop();
            mentionModel.clear();
            return;
        }

        mentionQuery = match[1];
        // One request per pause rather than one per keystroke - each is a round trip.
        mentionTimer.restart();
    }

    // People picked from the list who have no username, newline-joined and paired by
    // index. Their names went into the message as ordinary words, so the ids have to
    // ride along to the send for the entity that makes them mentions.
    property string pendingMentionIds: ""
    property string pendingMentionNames: ""

    function applyMention(username, name, userId) {
        var head = textArea.text.substring(0, textArea.cursorPosition);
        var start = head.lastIndexOf("@");

        if (start < 0)
            return;

        var tail = textArea.text.substring(textArea.cursorPosition);

        // With a username the @handle is the mention and Telegram resolves it on its own.
        // Without one the person's name goes in as plain text and the entity sent with
        // the message is the only thing pointing at them - which is how the official
        // clients do it too.
        var inserted = username !== "" ? "@" + username : name;

        textArea.text = head.substring(0, start) + inserted + " " + tail;
        // After the space, so the next word is typed rather than the mention re-edited.
        textArea.cursorPosition = start + inserted.length + 1;

        if (username === "") {
            pendingMentionIds += userId + "\n";
            pendingMentionNames += name + "\n";
        }

        mentionModel.clear();
    }

    function clearPendingMentions() {
        pendingMentionIds = "";
        pendingMentionNames = "";
    }

    Timer {
        id: mentionTimer

        interval: 250
        onTriggered: chatManager.searchMentions(root.mentionQuery)
    }

    Connections {
        target: chatManager || null
        ignoreUnknownSignals: true

        onMentionsFound: {
            mentionModel.clear();

            // A reply that arrived after the cursor left the mention is stale - the list
            // is already cleared and must stay that way.
            if (root.mentionQuery === "" && usernames.length > 0)
                return;

            // Paired by index, as ChatManager sends them.
            for (var i = 0; i < usernames.length; ++i)
                mentionModel.append({ username: usernames[i], name: names[i], userId: userIds[i] });
        }
    }

    // Through ChatManager, which resolves the chat and answers on profileReady - main.qml
    // pushes the page from there with a context of its own. The same path a tapped mention
    // and a tapped group member take, so a profile is built the same way however it was
    // reached.
    function openProfile() {
        chatManager.openProfile(chatContext.chatId);
    }

    // Built on demand: the page imports QtMobility.gallery, and if that module is
    // absent the component fails to load rather than throwing at startup. Creating it
    // here lets that failure be reported instead of the button doing nothing.
    // Kept and reused rather than destroyed after each pick: tearing a page down
    // while the pop transition is still running is how QML1 crashes, and one picker
    // per chat page is bounded anyway.
    // QtObject, not variant: a variant initialised to null reads back as undefined,
    // so the "=== null" this used to test was never true - the page was never built
    // and pageStack.push() got handed undefined.
    property QtObject photoPicker: null

    function openPhotoPicker() {
        if (!photoPicker) {
            var component = Qt.createComponent("PhotoPickerPage.qml");

            if (component.status !== Component.Ready) {
                console.debug("Photo picker unavailable:", component.errorString());
                appWindow.showInfoBanner(qsTr("NoPhotos"));
                return;
            }

            photoPicker = component.createObject(root);
            photoPicker.picked.connect(sendPhotos);
        }

        pageStack.push(photoPicker);
    }

    // paths is newline-joined, straight from the picker: one string all the way to
    // sendPhotos, which is the only representation that crosses QML1 without question.
    // One photo goes out as an ordinary message, several as one album.
    function sendPhotos(paths) {
        // Whatever is in the composer rides along as the caption, which is how
        // Telegram behaves and costs nothing here.
        messageModel.sendPhotos(paths, textArea.text, composeState.replyId);

        textArea.text = "";
        composeState.clear();
        listView.followLast = true;

        pageStack.pop();
    }

    // Built and kept the same way the photo picker is, and for the same reasons - see
    // the note above. Qt.labs.folderlistmodel ships in the Harmattan 1.2 sysroot, but
    // the status check costs nothing and keeps a missing import from being a dead
    // button.
    property QtObject filePicker: null

    function openFilePicker() {
        if (!filePicker) {
            var component = Qt.createComponent("FilePickerPage.qml");

            if (component.status !== Component.Ready) {
                console.debug("File picker unavailable:", component.errorString());
                appWindow.showInfoBanner(qsTr("ErrorOccurred"));
                return;
            }

            filePicker = component.createObject(root);
            filePicker.picked.connect(sendDocuments);
        }

        pageStack.push(filePicker);
    }

    // ponytail: one message per file rather than one album carrying the lot. TDLib does
    // group documents, but this client draws them as separate bubbles either way, and a
    // loop over the send path that already works beats a second album builder.
    function sendDocuments(paths) {
        var list = paths.split("\n");
        // Only the first carries the composer's text, the way an album's caption does.
        // Repeating it under every file would be noise.
        var caption = textArea.text;

        for (var i = 0; i < list.length; ++i) {
            if (list[i] === "")
                continue;

            messageModel.sendDocument(list[i], caption, composeState.replyId);
            caption = "";
        }

        textArea.text = "";
        composeState.clear();
        listView.followLast = true;

        pageStack.pop();
    }

    Connections {
        target: messageModel

        // The other half of settleIfLoaded: when the model finishes loading after the
        // rows have already landed, this is the signal that arrives second.
        onLoadingChanged: listView.settleIfLoaded()

        onFetchedPosition: {
            listView.positionViewAtIndex(numItems, ListView.Beginning);
            // Keep pulling while the loaded slice still does not fill the screen.
            // Deferred; see fillTimer for why calling it directly is a no-op.
            listView.beginFill();
        }

        // Not onCountChanged: that also fires when a page of older messages is
        // prepended, which would yank the view to the bottom mid-scrollback.
        onMessageAppended: {
            // A live message ends the opening sequence, whether or not the user has
            // touched the list yet - otherwise the two would fight over contentY.
            if (!listView.followLast) {
                settleTimer.stop()
                return
            }

            listView.positionViewAtEnd()
            // Arrived while you are looking at the bottom of the chat, so it has
            // been read the moment it lands.
            listView.markVisibleAsRead()

            // One call was not enough. The row that just landed is a MessageDelegate
            // whose Loader has not instantiated its bubble yet, so at this moment the
            // list is sizing it from the running average - and contentHeight grows again
            // as soon as the bubble is laid out, leaving the view short of the bottom
            // with the new message off screen. Exactly the problem the opening position
            // already solves by repositioning a few times, so it settles the same way.
            listView.beginSettle(true)
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }

    Component.onCompleted: {
        // The page exists and is bound; the delta from here to "messages-shown" is the
        // first getChatHistory round trip plus the list positioning itself.
        utils.mark("chatpage-completed")
    }

    // Retires this page's context, which is what closes the chat and disposes of its
    // message model. Whatever chat page is left underneath becomes the open one again.
    //
    // Runs on app shutdown as well as on leaving the page, and by then appWindow's
    // properties can already be gone - chatManager reads back null and this threw.
    // Nothing needs closing at that point: the process is going away and TDLib drops an
    // open chat with the connection.
    Component.onDestruction: {
        if (chatManager && chatContext)
            chatManager.popContext(chatContext.token)
    }
}
