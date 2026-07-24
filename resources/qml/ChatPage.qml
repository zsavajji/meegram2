import QtQuick 1.1
import com.nokia.meego 1.1
import MyComponent 1.0
import "components"

Page {
    id: root

    property variant chat: chatManager.selectedChat
    property variant chatInfo: chatManager.chatInfo
    property variant messageModel: chatManager.messageModel

    // Pending reply target. Named distinctly rather than living on the page root,
    // because MessageBubble also uses id: root and would shadow it.
    QtObject {
        id: replyState

        property variant messageId: 0
        property string senderName: ""
        property string preview: ""

        function setTarget(id, sender, text) {
            messageId = id;
            senderName = sender;
            preview = text;
        }

        function clear() {
            messageId = 0;
            senderName = "";
            preview = "";
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

                SheetButton {
                    id: backButton
                    anchors.left: parent.left
                    anchors.leftMargin: root.platformStyle.rejectButtonLeftMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("WidgetChats")
                    onClicked: pageStack.pop()
                }

                Row {
                    id: chatInfoRow
                    anchors.right: parent.right
                    anchors.rightMargin: root.platformStyle.rejectButtonLeftMargin
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 12
                    layoutDirection: Qt.RightToLeft

                    MaskedItem {
                        id: maskedItem
                        anchors.verticalCenter: parent.verticalCenter
                        height: 50
                        width: 50

                        mask: Image {
                            sourceSize.width: maskedItem.width
                            sourceSize.height: maskedItem.height
                            source: "qrc:/images/avatar-image-mask.png"
                        }

                        Image {
                            id: profilePhotoImage
                            anchors.fill: parent
                            sourceSize.width: maskedItem.width
                            sourceSize.height: maskedItem.height
                            asynchronous: true
                            smooth: true
                            fillMode: Image.PreserveAspectCrop
                            source: chat.photo && chat.photo.localPath !== "" ?
                                        "image://chatPhoto/" + chat.photo.localPath :
                                        "image://theme/icon-l-content-avatar-placeholder"
                        }

                        MouseArea {
                            anchors.fill: parent
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            text: utils.replaceEmoji(chatInfo.title)
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            width: 230
                        }

                        Label {
                            text: chatInfo.status
                            font {
                                weight: Font.Light
                                pixelSize: 20
                            }
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            width: 230
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

            highlightFollowsCurrentItem: true

            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: sectionDateDelegate

            ScrollDecorator { flickableItem: listView }

            onCountChanged: {
                if (!messageModel.loading && loading) {
                    if (chat.unreadCount > 0) {
                        listView.positionViewAtIndex(messageModel.lastMessageIndex(), ListView.Center)
                    } else {
                        listView.positionViewAtEnd()
                    }

                    loading = false
                }
            }

            onAtYBeginningChanged: {
                if (atYBeginning && !loading) {
                    console.log("Fetching more messages...")
                    messageModel.fetchMoreBack()
                }
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

            // Sits above the text field while a reply is pending. Collapses to zero
            // height when there is no target, so it costs nothing the rest of the time.
            Rectangle {
                id: replyBanner

                visible: replyState.messageId !== 0
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
                    text: replyState.senderName
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
                    text: replyState.preview
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
                        onClicked: replyState.clear()
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
                    text: "Send"
                    onClicked: {
                        messageModel.sendMessage(textArea.text, replyState.messageId)
                        textArea.text = ""
                        replyState.clear()
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

    Connections {
        target: messageModel

        onFetchedPosition: {
            listView.positionViewAtIndex(numItems, ListView.Beginning);
        }
    }

    Component.onDestruction: { chatManager.closeChat(chat.id) }
}
