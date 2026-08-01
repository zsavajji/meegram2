import QtQuick 1.1
import com.nokia.meego 1.1

// Who you are talking to, opened from the chat header. Bound to the current selection
// the same way ChatPage is - it is pushed on top of that page and popped before another
// chat can be opened, so there is no captured id to keep here.
Page {
    id: root

    property variant chat: chatManager.selectedChat
    property variant chatInfo: chatManager.chatInfo

    // The big photo is a separate file from the avatar the chat list uses, and nothing
    // has asked for it before this page.
    property bool hasPhoto: chat.bigPhoto && chat.bigPhoto.isDownloadingCompleted

    orientationLock: PageOrientation.LockPortrait

    // The rows under the photo, as { key, value } pairs - key being the language-pack
    // key, translated by the delegate. A function rather than three near-identical
    // blocks in the Column: the empty ones have to disappear, and empty is the normal
    // state for most of them - a group has no phone number, and a user who hides theirs
    // has none either.
    function details() {
        var rows = [];

        if (chatInfo.phoneNumber !== "")
            rows.push({ key: "PhoneMobile", value: chatInfo.phoneNumber });

        if (chatInfo.username !== "")
            rows.push({ key: "Username", value: chatInfo.username });

        if (chatInfo.bio !== "")
            rows.push({ key: "UserBio", value: chatInfo.bio });

        return rows;
    }

    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height
        clip: true

        Column {
            id: column

            width: flick.width

            Item {
                id: photoBox

                width: parent.width
                // A chat with no photo at all gets no black square and no placeholder -
                // the page starts at the name instead. visible follows the height because
                // an Item does not clip, so a centred child of a zero-height box would
                // otherwise draw straight over the title.
                height: chat.bigPhoto ? width : 0
                visible: height > 0

                Rectangle {
                    anchors.fill: parent
                    color: "black"
                }

                Image {
                    anchors.fill: parent
                    visible: root.hasPhoto
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    // Caps the decode at the size actually shown: the source is 640px
                    // square and the screen is 480.
                    sourceSize.width: width
                    // Straight off disk rather than through chatPhoto: that provider
                    // masks avatars round and caches every result, and one 480px entry
                    // is worth 30 of the avatars it is sized for.
                    source: root.hasPhoto ? "file://" + chat.bigPhoto.localPath : ""
                }

                Image {
                    anchors.centerIn: parent
                    visible: !root.hasPhoto
                    source: "image://theme/icon-l-content-avatar-placeholder"
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: !root.hasPhoto && chat.bigPhoto && chat.bigPhoto.isDownloadingActive
                    running: visible
                    platformStyle: BusyIndicatorStyle { size: "large" }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.hasPhoto
                    // The full-screen viewer already exists and already zooms.
                    onClicked: appWindow.openPhoto(chat.bigPhoto.localPath)
                }
            }

            Column {
                id: info

                width: parent.width - 32
                x: 16

                spacing: 16

                Item {
                    width: parent.width
                    height: 16
                }

                Label {
                    width: parent.width
                    text: utils.replaceEmoji(chatInfo.title)
                    textFormat: Text.RichText
                    font.pixelSize: 32
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    width: parent.width
                    text: chatInfo.status
                    color: "#505050"
                    font.pixelSize: 22
                    font.weight: Font.Light
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.details()

                    // info.width, not parent.width: a Repeater delegate is reparented
                    // onto the Column after it is built, so the binding is on nothing at
                    // the moment it first evaluates.
                    Column {
                        width: info.width
                        spacing: 4

                        Rectangle {
                            width: parent.width
                            height: 1
                            opacity: 0.5
                            color: "#cccccc"
                        }

                        Item {
                            width: parent.width
                            height: 8
                        }

                        Label {
                            width: parent.width
                            // The bio arrives as the HTML Utils::formattedText produces,
                            // so its links survive; the other two are plain and unharmed
                            // by being read as rich text.
                            text: modelData.value
                            textFormat: Text.RichText
                            font.pixelSize: 26
                            wrapMode: Text.WordWrap
                            onLinkActivated: Qt.openUrlExternally(link)
                        }

                        Label {
                            width: parent.width
                            // ponytail: language-pack keys taken from Telegram's Android
                            // pack. A key it does not carry shows as its own name; swap
                            // it if one turns up bare on device.
                            text: qsTr(modelData.key)
                            color: "#505050"
                            font.pixelSize: 20
                            font.weight: Font.Light
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 16
                }
            }
        }
    }

    ScrollDecorator {
        flickableItem: flick
    }

    tools: ToolBarLayout {
        ToolIcon {
            platformIconId: "toolbar-back"
            onClicked: appWindow.pageStack.pop()
        }
    }

    Component.onCompleted: {
        if (chat.bigPhoto && chat.bigPhoto.canBeDownloaded && !chat.bigPhoto.isDownloadingActive && !chat.bigPhoto.isDownloadingCompleted)
            appManager.downloadFile(chat.bigPhoto.id, 1, 0, 0, false);

        // The bio is the one thing here TDLib has to be asked for.
        chatInfo.loadProfile();
    }
}
