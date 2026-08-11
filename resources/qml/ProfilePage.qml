import QtQuick 1.1
import com.nokia.meego 1.1

// Who you are talking to, opened from the chat header, from a mention tapped inside a
// message, or from a member of the group whose profile is already open.
//
// Handed its own context at push time (main.qml, openProfilePage) and reads no shared
// state at all, which is what lets several of these stack. Binding to a single "current
// profile" on ChatManager is what made tapping a member rewrite this page and then push a
// second one showing the same person.
Page {
    id: root

    // Set once, by whoever pushed this page. Not what is being read either: a mention
    // opens somebody who is not the chat being viewed, and the page underneath - profile
    // or chat - stays on what it was showing. A profile context carries no message model.
    property variant chatContext

    property variant chat: chatContext.chat
    property variant chatInfo: chatContext.info

    // The big photo is a separate file from the avatar the chat list uses, and nothing
    // has asked for it before this page.
    property bool hasPhoto: chat && chat.bigPhoto && chat.bigPhoto.isDownloadingCompleted

    orientationLock: PageOrientation.LockPortrait

    // The rows under the photo, as { key, value } pairs - key being the language-pack
    // key, translated by the delegate. A function rather than three near-identical
    // blocks in the Column: the empty ones have to disappear, and empty is the normal
    // state for most of them - a group has no phone number, and a user who hides theirs
    // has none either.
    function details() {
        var rows = [];

        // As a tel: link, which is what hands it to the dialer: the row is already drawn as
        // rich text and already routes onLinkActivated through appWindow.openLink, whose
        // fallback is Qt.openUrlExternally - and that is the same path a phone number
        // tapped inside a message takes (Utils::formattedText emits the same anchor for a
        // phone-number entity). Nothing new to plumb, and it appears only where the number
        // does: phoneNumber is empty for a group and for anyone hiding theirs.
        //
        // The number arrives as "+" and the digits, ungrouped, so it needs no stripping to
        // be a valid tel: target.
        if (chatInfo.phoneNumber !== "")
            rows.push({ key: "PhoneMobile", value: '<a href="tel:' + chatInfo.phoneNumber + '">' + chatInfo.phoneNumber + '</a>' });

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
                            // The bio arrives as the HTML Utils::formattedText produces, so
                            // its links survive, and the phone number is wrapped in a tel:
                            // anchor by details() above. Only the username is plain, and it
                            // is unharmed by being read as rich text.
                            text: modelData.value
                            textFormat: Text.RichText
                            font.pixelSize: 26
                            wrapMode: Text.WordWrap
                            // A bio can carry mentions of its own, and they open the
                            // same way they do inside a message.
                            onLinkActivated: appWindow.openLink(link)
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

                // Who is in the group, most recently seen first. One snapshot, taken on
                // the way in: following the list live would mean a model and a
                // subscription for something nobody watches change while it is open.
                // Empty - and so hidden - in a private chat and in a channel.
                Column {
                    width: info.width
                    spacing: 4
                    // Up while the snapshot is on its way as well as once it has landed, so
                    // the heading is there from the start rather than the whole section
                    // appearing from nowhere when the list arrives. Both are false in a
                    // private chat and in a channel, where nothing is ever asked for.
                    visible: chatInfo.membersLoading || chatInfo.members.length > 0

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
                        text: qsTr("GroupMembers")
                        color: "#505050"
                        font.pixelSize: 20
                        font.weight: Font.Light
                    }

                    // No placeholder height reserved for it: Column skips invisible
                    // children, so this takes its own space while it spins and gives it
                    // back when the rows replace it. Default size, unlike the large one
                    // over the photo - this one sits in a list of 76px rows.
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: chatInfo.membersLoading
                        running: visible
                    }

                    Repeater {
                        model: chatInfo.members

                        Item {
                            id: memberRow

                            // Through a property first, the way the album cells take their
                            // photo: a QObject read straight off an element of a variant
                            // list is the conversion QML1 does not always make.
                            property variant photo: modelData.photo

                            width: info.width
                            height: 76

                            Image {
                                id: memberPhoto

                                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                                width: 56
                                height: 56

                                // Same provider as the chat list, so it arrives cropped,
                                // masked and cached at this size.
                                sourceSize.width: width
                                sourceSize.height: height
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: memberRow.photo && memberRow.photo.isDownloadingCompleted
                                            ? "image://chatPhoto/" + memberRow.photo.localPath
                                            : "image://theme/icon-l-content-avatar-placeholder"

                                Component.onCompleted: {
                                    var photo = memberRow.photo;

                                    if (photo && photo.canBeDownloaded && !photo.isDownloadingActive && !photo.isDownloadingCompleted)
                                        appManager.downloadFile(photo.id, 1, 0, 0, false);
                                }
                            }

                            Column {
                                anchors {
                                    left: memberPhoto.right
                                    leftMargin: 12
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                spacing: 2

                                Item {
                                    width: parent.width
                                    height: memberName.height

                                    Label {
                                        id: memberName

                                        anchors {
                                            left: parent.left
                                            right: memberTag.left
                                            rightMargin: memberTag.visible ? 8 : 0
                                        }
                                        // elideEmoji, not replaceEmoji: emoji markup makes
                                        // this rich text, and rich text ignores elide.
                                        text: utils.elideEmoji(modelData.name, font, width)
                                        font.pixelSize: 26
                                    }

                                    Label {
                                        id: memberTag

                                        anchors { right: parent.right; baseline: memberName.baseline }
                                        text: modelData.tag
                                        visible: text !== ""
                                        color: "#505050"
                                        font.pixelSize: 18
                                        font.weight: Font.Light
                                    }
                                }

                                Label {
                                    width: parent.width
                                    text: modelData.status
                                    color: "#505050"
                                    font.pixelSize: 20
                                    font.weight: Font.Light
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: chatManager.openProfile(modelData.userId)
                            }
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

        // Opening the conversation with whoever this is. Hidden when it is the chat you
        // came from, where it would only push the page underneath a second time - which
        // is every profile reached from a chat header, and none reached from a mention.
        ToolButton {
            text: qsTr("SendMessage")
            visible: chatContext.chatId !== chatManager.activeChatId
            onClicked: appWindow.openChat(chatContext.chatId)
        }
    }

    // Retires this page's context. It is owned by ChatManager, not here: QML1 gives a page
    // no way to own a QObject, and leaving it to the collector is how object lifetime goes
    // wrong in this codebase. Guarded because this also runs on app shutdown, by which
    // time appWindow's properties can already be gone - the same reason ChatPage guards
    // its own.
    Component.onDestruction: {
        if (chatManager && chatContext)
            chatManager.popContext(chatContext.token)
    }

    Component.onCompleted: {
        if (chat && chat.bigPhoto && chat.bigPhoto.canBeDownloaded && !chat.bigPhoto.isDownloadingActive && !chat.bigPhoto.isDownloadingCompleted)
            appManager.downloadFile(chat.bigPhoto.id, 1, 0, 0, false);

        // The bio is the one thing here TDLib has to be asked for.
        chatInfo.loadProfile();
        // And the members, for a group. Once per visit - the list is a snapshot, so
        // coming back here is what refreshes it.
        chatInfo.loadMembers();
    }
}
