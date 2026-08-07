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

    // A tapped notification that arrived before there was anything to open it with. See
    // openChat(); the flush is onChatManagerChanged below.
    property string pendingChatId: ""

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

        // TDLib has just authorized, which is the earliest a tap can be acted on. The
        // chat is often still not in the store this soon - openChat fetches it and
        // onChatAvailable finishes the push.
        onChatManagerChanged: {
            if (pendingChatId === "")
                return;

            var chatId = pendingChatId;
            pendingChatId = "";

            openChat(chatId);
        }
    }

    // For the handful of strings that exist before the language pack does - anything built
    // with this window rather than pushed later. QML1 has no retranslate, so a plain qsTr
    // out there keeps its first result for the life of the process; reading `initialized`
    // here is what re-runs the caller's binding once the pack is in. Until then qsTr hands
    // back the key, which is at least readable - the empty string this replaces was three
    // blank menu items whenever the pack was slow.
    function tr(key) {
        var retranslateOnInitialized = initialized;
        return qsTr(key);
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
        // A tap on a banner starts the app, and the D-Bus call carrying it is delivered
        // as soon as NotificationEndpoint owns the name - which is in AppManager's
        // constructor, before TDLib has authorized and so before chatManager exists.
        // This used to call straight into it: the null threw, the handler stopped there,
        // and the tap was gone for the rest of the run, leaving the launch it started
        // sitting on the chat list. Hold it until there is something to open it with.
        //
        // appManager.chatManager rather than the alias on appWindow: that alias is a
        // binding on the same signal that flushes this, and nothing orders the two. Same
        // reason MainPage reads it that way.
        var manager = appManager.chatManager;

        if (!manager) {
            pendingChatId = chatId;
            return;
        }

        // Push only if there is something to show. This used to push regardless, so a
        // chat that could not be selected produced a page with chat, chatInfo and
        // messageModel all undefined - a spinner that never resolved. A refusal means a
        // fetch is under way; onChatAvailable comes back with the outcome.
        if (!manager.openChat(chatId))
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
