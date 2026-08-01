import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Page {
    id: root

    property variant chat: chatManager.selectedChat
    property variant chatInfo: chatManager.chatInfo
    property variant messageModel: chatManager.messageModel

    // The chat this page was opened for, captured once. `chat` above is a live binding
    // to the current selection, so a page that has been popped but not yet destroyed
    // follows it onto whatever was opened next - and then closes the wrong chat on the
    // way out. Assigned imperatively because a declared initialiser would be a binding
    // and would do exactly that.
    property variant openedChatId: 0

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

        function open(id, sender, text, outgoing, file, name) {
            messageId = id;
            menuTarget.sender = sender;
            menuTarget.text = text;
            isOutgoing = outgoing;
            // Callers with nothing to save omit the argument entirely.
            saveFile = file || null;
            saveName = name || "";
            messageMenu.open();
        }
    }

    // Saving waits on the original size, which the bubble never needed and so has not
    // downloaded. Null once the save has gone through or when none is pending.
    property QtObject pendingSave: null

    // The name the pending save has to land under; see menuTarget.saveName.
    property string pendingSaveName: ""

    // One place decides where a file goes and what the banner says, so the save that
    // happens immediately and the one that waits for a download cannot drift apart.
    function commitSave(file, name) {
        var saved = name !== "" ? utils.saveDocument(file.localPath, name) : utils.saveToGallery(file.localPath);

        if (!saved) {
            appWindow.showInfoBanner(qsTr("ErrorOccurred"));
            return;
        }

        // ponytail: SavedToDownloads is a real language-pack key but an unusual one, so
        // it may fall back to showing its own name. Swap it if that turns up on device.
        appWindow.showInfoBanner(name !== "" ? qsTr("SavedToDownloads") : qsTr("PhotoSavedHint"));
    }

    function saveOriginal(file, name) {
        if (!file)
            return;

        name = name || "";

        if (file.isDownloadingCompleted) {
            commitSave(file, name);
            return;
        }

        pendingSave = file;
        pendingSaveName = name;
        appWindow.showInfoBanner(qsTr("Loading"));

        if (file.canBeDownloaded && !file.isDownloadingActive)
            appManager.downloadFile(file.id, 1, 0, 0, false);
    }

    Connections {
        target: pendingSave

        onFileChanged: {
            // Fires on the download starting as well as on it finishing, so completion
            // has to be checked rather than assumed.
            if (pendingSave && pendingSave.isDownloadingCompleted) {
                var file = pendingSave;
                var name = pendingSaveName;
                pendingSave = null;
                pendingSaveName = "";
                commitSave(file, name);
            }
        }
    }

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

                Row {
                    id: chatInfoRow
                    anchors.left: parent.left
                    anchors.leftMargin: root.platformStyle.rejectButtonLeftMargin
                    anchors.right: parent.right
                    anchors.rightMargin: root.platformStyle.rejectButtonLeftMargin
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
                                    "image://theme/icon-l-content-avatar-placeholder"

                        // Swallows taps on the avatar so they do not fall through to
                        // whatever is behind the header.
                        MouseArea {
                            anchors.fill: parent
                        }

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
                            text: utils.replaceEmoji(chatInfo.title)
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
                    color: "black"
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

            Timer {
                id: settleTimer

                property int ticks: 0

                interval: 60
                repeat: true

                onTriggered: {
                    listView.goToInitialPosition()
                    // Also the only reliable point to report what has been read: on
                    // countChanged the delegates do not exist yet, so indexAt fails,
                    // and onMovementEnded needs the user to actually drag.
                    listView.markVisibleAsRead()

                    if (++ticks >= 5) {
                        stop()
                        listView.fillViewport()
                    }
                }
            }

            function beginSettle() {
                settleTimer.ticks = 0
                settleTimer.restart()
            }

            onMovementStarted: settleTimer.stop()

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
            // Re-armed only by fetchedPosition, i.e. by a page that actually brought rows,
            // so a chat with no more history asks once and stops.
            function fillViewport() {
                if (loading || settleTimer.running || count === 0 || contentHeight > height)
                    return

                messageModel.fetchMoreBack()
            }

            // fetchedPosition is emitted from inside insertMessages, which runs before
            // handleHistoryResponse reaches cleanupFlags - so at that moment the model
            // still has m_backFetching set and fetchMoreBack() returns at its own guard.
            // Calling fillViewport() straight from the handler therefore did nothing at
            // all, and the chain above stopped after the single page settleTimer started.
            //
            // interval 0 fires on the next event-loop turn, by which point the response
            // handler has run to completion and the flag is clear.
            Timer {
                id: fillTimer

                interval: 0
                onTriggered: listView.fillViewport()
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter

            width: parent.width
            height: busyIndicator.height

            visible: messageModel.count === 0

            BusyIndicator  {
                id: busyIndicator

                anchors.horizontalCenter: parent.horizontalCenter
                // Was hardcoded true, so its animation timer kept running after the
                // message list populated and the parent Column went invisible.
                running: visible
                platformStyle: BusyIndicatorStyle { size: "large" }
            }
        }

        Column {
            id: inputPanelHolder

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            // Sits above the text field while a reply or an edit is pending. Collapses
            // to zero height otherwise, so it costs nothing the rest of the time.
            Rectangle {
                id: replyBanner

                visible: composeState.replyId !== 0 || composeState.editId !== 0
                width: parent.width
                height: visible ? 60 : 0
                color: "white"

                Rectangle {
                    id: replyBannerBar

                    anchors {
                        left: parent.left
                        leftMargin: 16
                        verticalCenter: parent.verticalCenter
                    }
                    width: 3
                    height: 40
                    color: "#0077A8"
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
                    color: "#0077A8"
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
                    color: "#505050"
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
                    color: "#505050"

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
                    color: "#cccccc"
                }
            }

            TextArea {
                id: textArea
                height: 64
                width: parent.width
                placeholderText: "Write your message here"
                platformStyle: TextAreaStyle {
                    background: "qrc:/images/messaging-textedit-background.png"
                    backgroundError: "qrc:/images/messaging-textedit-background.png"
                    backgroundDisabled: "qrc:/images/messaging-textedit-background.png"
                    backgroundSelected: "qrc:/images/messaging-textedit-background.png"
                    backgroundCornerMargin: 1
                }
            }

            Rectangle {
                id: controls
                anchors {
                    left: parent.left
                    right: parent.right
                }
                height: 0
                clip: true
                color: "white"

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    height: 1
                    opacity: 0.5
                    color: "#cccccc"
                }

                Label {
                    id: attachButton

                    anchors {
                        left: parent.left
                        leftMargin: 24
                        verticalCenter: parent.verticalCenter
                    }
                    text: icons.attach
                    font.family: icons.fontFamily
                    font.pixelSize: 36
                    color: attachArea.pressed ? "#0077A8" : "#505050"

                    MouseArea {
                        id: attachArea

                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: attachMenu.open()
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
                    text: composeState.editId !== 0 ? qsTr("Save") : "Send"
                    onClicked: {
                        // Whitespace-only counts as empty: TDLib rejects such a
                        // message, so there is nothing to gain by sending it.
                        if (textArea.text.trim() !== "") {
                            if (composeState.editId !== 0) {
                                messageModel.editMessage(composeState.editId, textArea.text)
                            } else {
                                messageModel.sendMessage(textArea.text, composeState.replyId)
                                // Your own message is always worth jumping to, even
                                // from halfway up the history. It arrives back as an
                                // update, so the follow flag is what carries this.
                                listView.followLast = true
                            }

                            textArea.text = ""
                            composeState.clear()
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
                        when: textArea.activeFocus
                        PropertyChanges { target: controls; height: 64 }
                    }
                ]
            }
        }
    }

    // Harmattan action menu, raised by a long press on a bubble. Items that cannot
    // apply to the target hide rather than grey out, which is what the platform does.
    ContextMenu {
        id: messageMenu

        MenuLayout {
            MenuItem {
                text: qsTr("Reply")
                onClicked: composeState.reply(menuTarget.messageId, menuTarget.sender, menuTarget.text)
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
                visible: menuTarget.isOutgoing && menuTarget.text !== ""
                onClicked: composeState.edit(menuTarget.messageId, menuTarget.text)
            }

            MenuItem {
                text: qsTr("Save")
                visible: menuTarget.saveFile !== null
                onClicked: root.saveOriginal(menuTarget.saveFile, menuTarget.saveName)
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
            photoPicker.photoSelected.connect(sendPhoto);
        }

        pageStack.push(photoPicker);
    }

    function sendPhoto(path) {
        // Whatever is in the composer rides along as the caption, which is how
        // Telegram behaves and costs nothing here.
        messageModel.sendPhoto(path, textArea.text, composeState.replyId);

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
            filePicker.fileSelected.connect(sendDocument);
        }

        pageStack.push(filePicker);
    }

    function sendDocument(path) {
        messageModel.sendDocument(path, textArea.text, composeState.replyId);

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
            // Deferred by a turn; see fillTimer for why calling it directly is a no-op.
            fillTimer.restart();
        }

        // Not onCountChanged: that also fires when a page of older messages is
        // prepended, which would yank the view to the bottom mid-scrollback.
        onMessageAppended: {
            // A live message ends the opening sequence, whether or not the user has
            // touched the list yet - otherwise the two would fight over contentY.
            settleTimer.stop()

            if (listView.followLast) {
                listView.positionViewAtEnd()
                // Arrived while you are looking at the bottom of the chat, so it has
                // been read the moment it lands.
                listView.markVisibleAsRead()
            }
        }
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }

    Component.onCompleted: openedChatId = chat ? chat.id : 0

    // Runs on app shutdown as well as on leaving the page, and by then appWindow's
    // properties can already be gone - chatManager reads back null and this threw.
    // Nothing needs closing at that point: the process is going away and TDLib drops an
    // open chat with the connection.
    Component.onDestruction: {
        if (chatManager && openedChatId)
            chatManager.closeChat(openedChatId)
    }
}
