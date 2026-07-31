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

    // Latched from appManager's one-shot appInitialized. It lives here rather than on a
    // page because appWindow is created once and never destroyed - a page that misses the
    // signal, or is recreated after it, can never recover it.
    property bool initialized: false

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

        onAppInitialized: appWindow.initialized = true

        // A system notification was tapped. Back out to the chat list first, so repeated
        // taps do not stack chat pages on top of each other. The chat list is the root
        // page, so popping to it is exactly what pop(null) does.
        onChatRequested: {
            pageStack.pop(null, true)
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

    Component.onCompleted: {
        theme.inverted = settings.invertedTheme

        // Starting the app is not a page's job - the root page used to own this, which
        // tied the whole startup sequence to the lifetime of one page.
        appManager.initialize()
    }
}
