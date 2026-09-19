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

    // Dark theme. The platform inverts everything it draws itself off theme.inverted -
    // page background, Labels with no colour of their own, toolbars, list highlights - so
    // what is left to decide is the literals this app paints itself.
    //
    // Three surfaces, darkest at the back: the page behind everything (the platform's,
    // see below), the panels that sit on it, and the hairlines between them.
    property color secondaryColor: theme.inverted ? "#8c8c8c" : "#505050"
    property color separatorColor: theme.inverted ? "#3c3c3c" : "#cccccc"
    property color panelColor: theme.inverted ? "#1e1e1e" : "white"

    // The accent, for anything painted on a page or a panel. #0077A8 was picked against
    // white and is too dark to read on one of those surfaces, so the dark theme lightens
    // it rather than keeping one value for both.
    property color accentColor: theme.inverted ? "#4FC3E8" : "#0077A8"

    // And the accent for anything painted on a *bubble*, which is a different question:
    // the bubble brings its own background, so these follow the bubble asset and not the
    // page. Only the incoming side needs them - the outgoing bubble is the accent colour
    // in both themes and white reads on it either way, which is why every call site
    // spells it `isOutgoing ? "white" : <one of these>`.
    property color bubbleTextColor: theme.inverted ? "#ffffff" : "black"
    property color bubbleSecondaryColor: theme.inverted ? "#9a9a9a" : "#505050"
    property color bubbleAccentColor: theme.inverted ? "#4FC3E8" : "#0077A8"

    // Interactive glyphs - the composer's attach, record and emoji buttons, the emoji
    // picker's tabs. Brighter than secondaryColor because they are controls rather than
    // captions: #505050 on a dark panel is a button that reads as disabled.
    property color iconColor: theme.inverted ? "#c8c8c8" : "#505050"

    // Bubbles on, or the flat layout. Off means no balloons: every message runs full
    // width on the page and is told apart by an avatar and a coloured name rather than by
    // which side of the screen it sits on.
    property bool showBubbles: settings.showBubbles

    // Whether a message is *drawn* as an outgoing one - sided right, on the accent
    // balloon, with white text on it. Nearly every `model.isOutgoing ?` in a delegate is
    // asking this rather than asking who sent it, and with bubbles off the answer is
    // always no: what is left is the incoming layout, which is already a full-width row
    // on the page. The ones that genuinely ask who sent it - the action menu, the
    // unplayed-note dot - keep reading model.isOutgoing directly.
    function isSided(outgoing) {
        return outgoing && showBubbles;
    }

    // A wash for something sitting *on* a message - a reaction pill, a reply quote.
    // Lighter on the accent balloon, darker on everything else, and the other way round
    // in the dark theme, where a black wash on a dark bubble is nothing at all.
    function tintOn(sided) {
        return sided ? "#40ffffff" : theme.inverted ? "#20ffffff" : "#20000000";
    }

    // Theme glyphs are a different problem from the colours above, because they are
    // images and QML1 cannot tint one: there is no QtGraphicalEffects and no
    // ColorOverlay. Blanco draws them as near-black line art on transparent - #000000
    // exactly - so on a dark page they are there and invisible.
    //
    // The platform's answer is a second asset, and the suffix is `-inverse`, not the
    // `-inverted` the larger graphics use. Verified on device: the two files hold the
    // same glyph at the same pixel count, one black and one #f1f1f2.
    function themeIcon(name) {
        return "image://theme/" + name + (theme.inverted ? "-inverse" : "");
    }

    // The avatar placeholder cannot go through themeIcon: the `l` size this app uses has
    // no inverse variant at all, so the dark theme takes the `m` one, which does. That
    // asset is also a filled disc rather than bare line art, which is what the platform's
    // own dark screens show - so a placeholder gains a disc in dark, and drops from 80px
    // to 64px anywhere the call site does not size it.
    property url avatarPlaceholder: theme.inverted ? "image://theme/icon-m-content-avatar-placeholder-inverse"
                                                   : "image://theme/icon-l-content-avatar-placeholder"

    // The page behind everything is left at the platform's own inverted default, which
    // is #000000 - a flat Rectangle, since PageStackWindowStyle only reaches for the
    // theme's background image when `background` is set, and that image is a flat
    // #010101 tile anyway. Lifting the page to a dark grey was tried and dropped: every
    // other dark app on the device is black behind its content and lifts only its
    // chrome, so a grey page reads as the odd one out. The glare it was meant to fix
    // belongs to the accent, which is where it is fixed - see TopBar.
    //
    // If a gradient is ever wanted, it is `background` set to an image plus
    // `backgroundFillMode: Image.Stretch`; the platform ships no gradient for this.

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

        onAppInitialized: {
            utils.mark("app-initialized")
            appWindow.initialized = true
        }

        // A system notification was tapped. Back out to the chat list first, so repeated
        // taps do not stack chat pages on top of each other. The chat list is the root
        // page, so popping to it is exactly what pop(null) does.
        onChatRequested: {
            // The notification tap lands here. On a cold start this fires from
            // AppManager's constructor, so it can be the earliest marker of all - which
            // is the point: everything after it is latency the user is watching.
            utils.mark("notification-tap")

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

            // Closes the gap opened at "chat-open-deferred": TDLib has authorized and the
            // held tap can finally be acted on.
            utils.mark("chat-open-flushed")

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

        // A tapped mention, or the chat header's own profile button - both go through
        // openProfile and land here once there is a profile to show.
        onProfileReady: {
            if (ok) {
                openProfilePage(chatId)
                return
            }

            // The reason as ChatManager gives it, untranslated, rather than a tidy
            // "No results": this device collects no log and its daemon socket refuses
            // every peer but the app, so a failure that is not on screen cannot be
            // looked into at all. Swap for qsTr("NoResult") once it stops failing.
            showInfoBanner(reason !== "" ? reason : qsTr("NoResult"))
        }
    }

    // A link tapped inside a message. Utils::formattedText turns each entity into an href
    // with its own scheme, so this is where they are told apart. Anything not handled
    // here goes to the browser, which is what a plain url has always done.
    function openLink(link) {
        // "mention:@name" carries the username; "mention_name:<user id>" is a mention of
        // somebody without one, and a user id is also the id of the private chat with
        // them. openProfile takes either.
        if (link.indexOf("mention:") === 0) {
            chatManager.openProfile(link.substring(8))
            return
        }

        if (link.indexOf("mention_name:") === 0) {
            chatManager.openProfile(link.substring(13))
            return
        }

        // ponytail: hashtags, cashtags and bot commands have nowhere to go in this client
        // yet. Swallowed rather than handed to the browser, which would open a search for
        // the literal "hashtag:#foo".
        if (link.indexOf("hashtag:") === 0 || link.indexOf("cashtag:") === 0 || link.indexOf("botCommand:") === 0)
            return

        Qt.openUrlExternally(link)
    }

    // Pushed from here rather than by whoever tapped the mention: the chat may have had
    // to be fetched or created first, so only ChatManager knows when there is something
    // to bind to.
    //
    // Each page gets its own context and binds only to what it was handed here. Nothing on
    // the page reads a "current profile", which is what used to make tapping a member
    // rewrite the page underneath and then push a copy of it - the two were the same
    // object. A profile context carries no message model and does not change which chat is
    // being read, so opening one over a conversation leaves that conversation open.
    function openProfilePage(chatId) {
        var component = Qt.createComponent("ProfilePage.qml");

        if (component.status !== Component.Ready) {
            console.debug("Error loading component:", component.errorString());
            return;
        }

        // After the component is known to be good: a push that cannot happen must not
        // leave a context behind for a page that was never created. Nothing to overlap
        // here, unlike openChat above - a profile starts no history fetch.
        var chatContext = chatManager.pushProfile(chatId);

        if (!chatContext) {
            showInfoBanner(qsTr("ErrorOccurred"));
            return;
        }

        pageStack.push(component, { chatContext: chatContext });
    }

    // Lives here rather than on ChatPage so the delegate can reach it by a unique name -
    // "root" inside MessageDelegate resolves to ChatPage's root and is easy to get wrong.
    //
    // original is the full-size File behind the photo, for the viewer's Save. Optional:
    // a caller with nothing to save opens a picture that simply cannot be kept.
    function openPhoto(path, original) {
        var component = Qt.createComponent("PhotoViewPage.qml");

        if (component.status !== Component.Ready) {
            console.debug("Error loading component:", component.errorString());
            return;
        }

        pageStack.push(component, { source: "file://" + path, original: original || null });
    }

    // Saving lives here rather than on ChatPage: the bubble menu, the fullscreen viewer
    // and the album batch all save the same way, and only this object is reachable from
    // all three.
    //
    // The original size is what gets kept - the bubble only ever downloaded the size that
    // covers the screen - so a save usually has to wait for a download first. Null when
    // nothing is pending.
    property QtObject pendingSave: null

    // The name the pending save has to land under; see menuTarget.saveName on ChatPage.
    property string pendingSaveName: ""

    // One place decides where a file goes and what the banner says, so the save that
    // happens immediately and the one that waits for a download cannot drift apart.
    function commitSave(file, name) {
        var saved = name !== "" ? utils.saveDocument(file.localPath, name) : utils.saveToGallery(file.localPath);

        if (!saved) {
            showInfoBanner(qsTr("ErrorOccurred"));
            albumIndex = -1;
            return;
        }

        // One banner per batch rather than one per photo: a dozen of them queue up and
        // sit on the screen long after the last save.
        if (albumIndex < 0)
            // FileSavedHint, checked against the pack on the device: SavedToDownloads was
            // not a key at all - only prefixed forms of it are - so this banner read
            // "SavedToDownloads" whenever a named file was saved. The named branch is a
            // document going to the downloads folder, which is exactly what this says;
            // PhotoSavedHint on the other branch is a real key and was always right.
            showInfoBanner(name !== "" ? qsTr("FileSavedHint") : qsTr("PhotoSavedHint"));
        else
            saveNextOfAlbum();
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
        showInfoBanner(qsTr("Loading"));

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

    // The album being saved photo by photo, and how far through it is. Serial because
    // pendingSave is one slot: firing every photo at once would leave all but the last
    // download unsaved, and a dozen parallel downloads is not what this radio wants
    // anyway. Held as the list the model handed over, untouched.
    property variant albumPhotos
    property int albumIndex: -1

    function saveAlbum(photos) {
        if (!photos || photos.length === 0)
            return;

        albumPhotos = photos;
        albumIndex = 0;
        saveNextOfAlbum();
    }

    function saveNextOfAlbum() {
        if (albumIndex < 0 || albumIndex >= albumPhotos.length) {
            albumIndex = -1;
            showInfoBanner(qsTr("PhotoSavedHint"));
            return;
        }

        // Through a variant property, not albumPhotos[i].originalFile: an element read
        // straight out of a model list is a QVariant QML1 never unwraps, so the property
        // comes back undefined. Same route the album's own cells take.
        albumCursor.photo = albumPhotos[albumIndex];
        ++albumIndex;

        var file = albumCursor.photo ? albumCursor.photo.originalFile : null;

        // A photo with nothing behind it must not end the batch: saveOriginal returns
        // on a null file, and the chain is driven from its completion.
        if (!file) {
            saveNextOfAlbum();
            return;
        }

        saveOriginal(file, "");
    }

    QtObject {
        id: albumCursor

        property variant photo
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
            // On a notification-tap launch this is the normal outcome, not an error: the
            // tap beat TDLib's authorization. The gap from here to "chat-open-flushed"
            // is dead time the user spends looking at the chat list they did not ask for.
            utils.mark("chat-open-deferred")
            pendingChatId = chatId;
            return;
        }

        utils.mark("chat-open-begin")

        // Push only if there is something to show. This used to push regardless, so a
        // chat that could not be selected produced a page with chat, chatInfo and
        // messageModel all undefined - a spinner that never resolved. A refusal means a
        // fetch is under way; onChatAvailable comes back with the outcome.
        //
        // The context is this page's own - its chat, its formatter, its message model -
        // so a second ChatPage on the stack no longer rebinds the first one onto the new
        // conversation. It is taken before the compile below so the first history request
        // is already on the socket while ChatPage is being compiled.
        var chatContext = manager.pushChat(chatId);

        if (!chatContext)
            return;

        // Separated so the compile below is attributable on its own: pushChat() is the
        // C++ side opening the chat and kicking off the first history fetch.
        utils.mark("chat-selected")

        var component = Qt.createComponent("ChatPage.qml");

        if (component.status !== Component.Ready) {
            console.debug("Error loading component:", component.errorString());
            // Give the context back rather than leaving one on the stack for a page that
            // will never exist - it holds an open chat and a live message model.
            manager.popContext(chatContext.token);
            return;
        }

        // Brackets the compile of ChatPage.qml, which is the largest file in the scene
        // and is not compiled until the first chat is opened - so a cold start pays it
        // here rather than in setSource.
        utils.mark("chatpage-compiled")

        pageStack.push(component, { chatContext: chatContext });

        utils.mark("chatpage-pushed")
    }

    Component.onCompleted: {
        theme.inverted = settings.invertedTheme

        // Startup markers. This one is the first thing the event loop runs, so the gap
        // back to "shown" is what QApplication::exec costs before it reaches us.
        utils.mark("qml-oncompleted")

        // Starting the app is not a page's job - the root page used to own this, which
        // tied the whole startup sequence to the lifetime of one page.
        appManager.initialize()

        // initialize() returns as soon as the request is away; everything real about it
        // is asynchronous. The delta to onAppInitialized is the daemon handshake plus
        // TDLib authorizing, which is the phase no C++ marker can reach.
        utils.mark("initialize-returned")
    }
}
