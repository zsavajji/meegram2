import QtQuick 1.1
import com.nokia.meego 1.1
import com.nokia.extras 1.1
import MyComponent 1.0
import "components"

PageStackWindow {
    id: appWindow

    property variant client: appManager.client
    property variant authorization: appManager.authorization
    property variant locale: appManager.locale
    property variant settings: appManager.settings
    property variant storageManager: appManager.storageManager
    property variant chatManager: appManager.chatManager

    property bool isPortrait: screen.currentOrientation !== Screen.Landscape

    initialPage: Component { MainPage {} }

    onOrientationChangeFinished: showStatusBar = isPortrait

    Icons { id: icons }

    InfoBanner {
        id: banner
        y: 36
        z: 100
    }

    Connections {
        target: settings
        onInvertedThemeChanged: theme.inverted = settings.invertedTheme
    }

    Connections {
        target: appManager

        // A system notification was tapped. Back out to the chat list first, so
        // repeated taps do not stack chat pages on top of each other.
        //
        // Down to depth 2, not pop(null). The chat list is not the root page: MainPage
        // is, and it is the startup spinner with ChatsPage pushed on top of it once,
        // when appInitialized fires. pop(null) means "pop down to the first page", so it
        // took ChatsPage with it and left the app on a spinner that never resolves
        // again, with no way back short of restarting.
        onChatRequested: {
            while (pageStack.depth > 2)
                pageStack.pop(undefined, true)

            openChat(chatId)
        }
    }

    function showInfoBanner(message) {
        banner.text = message
        banner.show()
    }

    Connections {
        // chatManager is null until authorizationStateReady, so at load time this is
        // undefined and Qt4 warns twice - once that the target cannot be assigned, once
        // that onChatAvailable does not exist. The binding re-targets when AppManager
        // emits chatManagerChanged, so the connection does work; both lines are noise
        // that reads exactly like a dead connection in the only log we debug from.
        target: chatManager || null
        ignoreUnknownSignals: true

        // A chat opened by id is not always cached yet - Saved Messages opens myId()
        // directly, and a notification can be tapped before the chat list has loaded.
        // ChatManager fetches it and reports back here rather than failing the open.
        onChatAvailable: {
            if (ok)
                openChat(chatId)
            else
                showInfoBanner(qsTr("ErrorOccurred"))
        }
    }

    // Lives here rather than on ChatPage so the delegate can reach it by a unique name -
    // "root" inside MessageDelegate resolves to ChatPage's root and is easy to get wrong.
    function openPhoto(path) {
        var component = Qt.createComponent("PhotoViewPage.qml");

        if (component.status !== Component.Ready) {
            console.debug("Error loading component:", component.errorString());
            return;
        }

        pageStack.push(component, { source: "file://" + path });
    }

    function openChat(chatId) {
        // Every caller routes through here - the chat list, Saved Messages, a tapped
        // notification, the fetch retry. A failure with no "tap row" line before it did
        // not come from the list.
        utils.log("openChat requested for " + chatId)

        // Push only if there is something to show. This used to push regardless, so a
        // chat that could not be selected produced a page with chat, chatInfo and
        // messageModel all undefined - a spinner that never resolved. A refusal means a
        // fetch is under way; onChatAvailable comes back with the outcome.
        if (!chatManager.openChat(chatId))
            return;

        var component = Qt.createComponent("ChatPage.qml");

        if (component.status !== Component.Ready) {
            console.debug("Error loading component:", component.errorString());
            return;
        }

        pageStack.push(component);
    }

    Component.onCompleted: theme.inverted = settings.invertedTheme
}
