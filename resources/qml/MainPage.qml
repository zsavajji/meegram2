import QtQuick 1.1
import com.nokia.meego 1.1
import com.nokia.extras 1.1
import MyComponent 1.0
import "components"

Page {
    id: root
    orientationLock: PageOrientation.LockPortrait

    // chatManager does not exist until TDLib reports authorizationStateReady, and this
    // page is now built at launch rather than after sign-in - so these have to survive it
    // being absent. They re-evaluate on chatManagerChanged.
    //
    // Read straight off appManager rather than through appWindow.chatManager: that alias
    // is itself a binding on the same signal, and nothing orders it before this one.
    property variant chatFolderModel: appManager.chatManager ? appManager.chatManager.folderModel : null
    property variant mainChatModel: appManager.chatManager ? appManager.chatManager.mainModel : null
    property variant folderChatModels: appManager.chatManager ? appManager.chatManager.folderModels : null

    TopBar {
        id: header
        title: "MeeGram"
    }

    // The whole app in one binding: still starting up, started but signed out, signed in.
    // Nothing is pushed to get here, so this page is always the bottom of the stack.
    //
    // appWindow.initialized gates everything because Locale is a QTranslator whose strings
    // only arrive with the language pack, and QML1 never retranslates - anything built
    // before that stays untranslated for the life of the process.
    //
    // The signed-in test is chatManager, not appManager.authorized: authorizedChanged is
    // emitted before chatManager is constructed, and ChatListView calls model.refresh() as
    // soon as it is built. Keying on chatManager closes that window without guards.
    Loader {
        id: layoutLoader
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        sourceComponent: !appWindow.initialized ? busyComponent
                       : !appManager.chatManager ? infoComponent
                       : folderChatModels.count === 0 ? chatLayoutComponent
                                                      : chatTabsLayoutComponent
    }

    Component {
        id: busyComponent

        Item {
            anchors.fill: parent

            BusyIndicator {
                anchors.centerIn: parent
                running: true
                platformStyle: BusyIndicatorStyle { size: "large" }
            }
        }
    }

    Component {
        id: infoComponent

        Item {
            anchors.fill: parent

            Column {
                spacing: 20
                width: parent.width

                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    topMargin: 30
                }

                Text {
                    id: infoText
                    text: "Different, Handy, Powerful"
                    wrapMode: Text.Wrap
                    font.pixelSize: 30
                    color: "#777777"
                    horizontalAlignment: Text.AlignHCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                }

                Button {
                    text: qsTr("StartMessaging")
                    platformStyle: ButtonStyle { inverted: true }
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        var component = Qt.createComponent("AuthenticationPage.qml")
                        if (component.status === Component.Ready) {
                            var authenticationSheet = component.createObject(root);
                            authenticationSheet.open();
                        }
                    }
                }
            }
        }
    }

    Component {
        id: chatLayoutComponent

        Item {
            anchors.fill: parent

            ChatListView { model: mainChatModel }
        }
    }

    Component {
        id: chatTabsLayoutComponent

        Item {
            id: chatTabsLayout

            property alias tabFlickable: tabRowFlickable

            anchors.fill: parent
            Flickable {
                id: tabRowFlickable
                contentWidth: tabButtonRow.width
                height: 48
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                }

                Row {
                    id: tabButtonRow
                    property int buttonMargin: 1
                    anchors.verticalCenter: parent.verticalCenter

                    TabButton {
                        id: allChatsTabButton
                        text: qsTr("FilterAllChats")
                        tab: allChatsTab
                        checked: tabGroup.currentTab === allChatsTab
                        onClicked: {
                            tabGroup.currentTab = allChatsTab;
                            root.ensureVisible(allChatsTabButton);
                        }
                        width: allChatsTabText.width + tabButtonRow.buttonMargin * 2

                        Text {
                            id: allChatsTabText
                            text: parent.text
                            visible: false // Invisible, used only for width calculation
                        }
                    }

                    Repeater {
                        model: chatFolderModel

                        TabButton {
                            id: folderTabButton
                            text: model.name
                            checked: tabGroup.currentTab === tabGroup.children[index + 1]
                            onClicked: {
                                tabGroup.currentTab = tabGroup.children[index + 1];
                                root.ensureVisible(folderTabButton);
                            }
                            width: folderTabText.width + tabButtonRow.buttonMargin * 2

                            Text {
                                id: folderTabText
                                text: parent.text
                                visible: false // Invisible, used only for width calculation
                            }
                        }
                    }
                }

                function smoothScrollTo(targetX) {
                    contentXAnimation.to = targetX;
                    contentXAnimation.running = true;
                }

                NumberAnimation {
                    id: contentXAnimation
                    target: tabRowFlickable
                    property: "contentX"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }

            TabGroup {
                id: tabGroup
                currentTab: allChatsTab
                anchors {
                    top: tabRowFlickable.bottom
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }

                Item {
                    id: allChatsTab
                    anchors.fill: parent

                    ChatListView {
                        model: mainChatModel
                        clip: true
                    }
                }

                Repeater {
                    model: folderChatModels

                    Item {
                        id: folderTabPage
                        anchors.fill: parent

                        // Captured here so the lazily-created ChatListView below does
                        // not have to resolve modelData from the Repeater scope.
                        property variant folderModel: modelData

                        // Repeater is eager: without this, opening the chats page built
                        // a ChatListView for every folder, each running model.refresh()
                        // and its own 200ms populate timer, while only one tab is ever
                        // visible. Build a folder's list the first time its tab shows.
                        Loader {
                            id: folderListLoader
                            anchors.fill: parent
                        }

                        Component {
                            id: folderListComponent

                            ChatListView {
                                model: folderTabPage.folderModel
                                clip: true
                            }
                        }

                        function ensureLoaded() {
                            if (visible && !folderListLoader.sourceComponent)
                                folderListLoader.sourceComponent = folderListComponent;
                        }

                        onVisibleChanged: ensureLoaded()
                        Component.onCompleted: ensureLoaded()
                    }
                }

                onCurrentTabChanged: {

                }
            }
        }
    }

    Menu {
        id: myMenu

        // The one place on this page that qsTr is read outside the initialized gate: the
        // menu is built with the page, which is now the root page, so it is created before
        // the language pack can possibly have arrived and QML1 never retranslates. Naming
        // appWindow.initialized in the binding is what re-runs qsTr once it does - without
        // it these three read out as their keys for the life of the process.
        MenuLayout {
            MenuItem {
                text: appWindow.initialized ? qsTr("SavedMessages") : ""
                onClicked: appWindow.openChat(storageManager.myId())
            }
            MenuItem {
                text: appWindow.initialized ? qsTr("ArchivedChats") : ""
                onClicked: pageStack.push(Qt.createComponent("ArchivedChatPage.qml"), { model: chatManager.archivedModel })
            }
            MenuItem {
                text: appWindow.initialized ? qsTr("SETTINGS") : ""
                onClicked: pageStack.push(Qt.createComponent("SettingsPage.qml"))
            }
            MenuItem {
                text: "About"
                onClicked: aboutDialog.open()
            }
        }
    }

    AboutDialog {
        id: aboutDialog
    }

    // Every item in the menu dereferences chatManager, so it cannot be offered before
    // sign-in. About stays reachable either way: the help icon here, the menu item there.
    tools: ToolBarLayout {
        ToolIcon {
            anchors.right: (parent !== undefined) ? parent.right : undefined
            visible: !appManager.chatManager
            iconSource: "qrc:/images/help-icon.png"
            onClicked: aboutDialog.open()
        }
        ToolIcon {
            platformIconId: "toolbar-view-menu"
            anchors.right: (parent !== undefined) ? parent.right : undefined
            visible: !!appManager.chatManager
            onClicked: (myMenu.status === DialogStatus.Closed) ? myMenu.open() : myMenu.close()
        }
    }

    function ensureVisible(item) {
        if (layoutLoader.item && layoutLoader.item.tabFlickable) {
            var flickable = layoutLoader.item.tabFlickable;
            var targetContentX = item.x + item.width / 2 - flickable.width / 2;
            var clampedContentX = Math.max(0, Math.min(targetContentX, flickable.contentWidth - flickable.width));
            flickable.smoothScrollTo(clampedContentX);
        }
    }
}
