# MeeGram architecture

How the pieces fit together. Derived from reading the source plus a knowledge-graph
pass (`graphify-out/`), with every claim checked against the code.

MeeGram is a **Qt 4.7.4 / QtDeclarative (QML1)** Telegram client built on **TDLib**.
QML1 matters throughout: there is no scene graph, no per-row role cache in views,
and `QDeclarativeView` is a `QGraphicsView`, so rendering is CPU rasterisation
unless a GL viewport is installed.

---

## The shape of it

```
                      TDLib (static, C++ interface)
                                 │
                    Client  ── worker std::jthread ──┐
                    (src/Client.cpp)                 │  receive(30.0) loop
                                 │                   │
                    emit result(Object*)  ── queued to main thread
                                 │
        ┌────────────┬───────────┴───────────┬──────────────┐
        ▼            ▼                       ▼              ▼
  StorageManager  AppManager           Authorization   MessageModel
  entity cache    auth + connection     login flow     per-open-chat
        │         state only
        │  11 signals (7 consumed)
        ▼
   ChatModel ×N ── ChatManager ── ChatInfoFormatter
        │              │
        └──────────────┴────────► QML  (via appManager only)
```

**Every TDLib update is delivered to four independent handlers.** Each runs its own
`switch` and mostly falls through `default:`:

| Subscriber | Handles |
|---|---|
| `StorageManager` (`src/StorageManager.cpp:11`) | ~18 entity updates; the only cache |
| `AppManager` (`src/AppManager.cpp:229`) | `updateAuthorizationState`, `updateConnectionState` only |
| `Authorization` (`src/Authorization.cpp:17`) | login flow |
| `MessageModel` (`src/MessageModel.cpp:26`) | message updates for one chat |

---

## Ownership

`AppManager` is the composition root. Five subsystems are built in its constructor
initialiser list (`src/AppManager.cpp:222-226`):

```
AppManager
├── Client            shared_ptr   TDLib transport + worker thread
├── Authorization     shared_ptr   login state machine
├── Locale            shared_ptr   QTranslator subclass, installed app-wide
├── Settings          shared_ptr   QSettings-backed prefs
├── StorageManager    shared_ptr   entity cache
├── ChatManager       shared_ptr   *** created lazily, on auth ready ***
└── LanguagePackInfoModel  unique_ptr
```

`ChatManager` is the exception: it is constructed only when
`authorizationStateReady` arrives (`AppManager.cpp:423`), guarded against
re-creation, and announced via `chatManagerChanged` so QML bindings re-resolve
mid-session.

`ChatManager` in turn owns the list models (`src/ChatManager.hpp:115-124`):

```
ChatManager
├── m_mainModel        ChatModel        Main chat list
├── m_archivedModel    ChatModel        Archive
├── m_folderModels     vector<ChatModel>  one per Telegram folder
├── m_folderModel      ChatFolderModel  the folder tab strip itself
├── m_selectedChat     shared_ptr<Chat>
├── m_infoFormatter    ChatInfoFormatter  title/status text for the open chat
└── m_messageModel     MessageModel     messages of the open chat
```

---

## Data flow: one incoming message

1. TDLib worker thread receives `updateChatLastMessage`.
2. `Client::initialize` emits `result(...)` — **queued**, so it lands on the main
   thread. Four `handleResult` calls are posted; a fifth queued call disposes of the
   update object after they drain (`src/Client.cpp`).
3. `StorageManager::handleResult` mutates `m_chats[id]`, then emits `chatUpdated`
   **and** `chatPositionUpdated`.
4. `ChatModel::handleChatItem` resolves the row via its `chatId → row` index, drops
   the cached formatting for that chat, emits `dataChanged`.
5. `ChatModel::handleChatPosition` starts a **500 ms single-shot timer** so bursts of
   reordering coalesce into one sort.
6. The view re-reads roles; the formatter-backed ones (`title`, `date`,
   `lastMessage`) come from a per-chat cache instead of re-running
   `Utils::getChatTitle` / `getMessageDate` / `getContent` on every binding read.

---

## StorageManager: the hub

The highest-betweenness node in the codebase (0.105). It plays three roles at once,
and it is the *combination* that makes it central:

- **Fan-in** — sole listener on the raw TDLib stream for entity updates
- **Cache** — `m_chats`, `m_users`, `m_basicGroup`, `m_supergroup`,
  `m_supergroupFullInfo`, `m_files`, `m_chatFolders`, `m_options`
- **Fan-out** — 11 Qt signals

Consumers depend on it **twice**: push (signal) and pull (`shared_ptr` member).

| Signal | Emitted | Consumer |
|---|---|---|
| `chatUpdated` | 11 sites | `ChatModel.cpp:29` |
| `chatPositionUpdated` | 3 sites | `ChatModel.cpp:30` |
| `userUpdated` | 2 sites | `ChatManager.cpp:90` |
| `basicGroupUpdated` | 1 | `ChatManager.cpp:98` |
| `supergroupUpdated` | 1 | `ChatManager.cpp:105` |
| `chatOnlineMemberCountUpdated` | 1 | `ChatManager.cpp:91` |
| `chatFoldersUpdated` | 1 | `ChatManager.cpp:244` |
| `fileUpdated` | 1 | **none** |
| `supergroupFullInfoUpdated` | 1 | **none** |
| `basicGroupFullInfoUpdated` | **0** | **none** |
| `userFullInfoUpdated` | **0** | **none** |

Only 7 of 11 do any work. The last two are declared but never emitted *and* never
consumed — dead API surface.

---

## The C++/QML boundary

QML sees exactly **three** context properties (`src/main.cpp:124-127`) plus one
image provider:

```cpp
setContextProperty("appManager", &appManager);   // the only door to the object graph
setContextProperty("utils", &utils);             // static formatters
setContextProperty("AppVersion", AppVersion);
addImageProvider("chatPhoto", new ChatPhotoProvider);   // image://chatPhoto/<path>
```

Everything else is reached by walking `appManager.*`, which re-exports each
subsystem as a `Q_PROPERTY` (`src/AppManager.hpp:26-34`). That is why `AppManager`
has high betweenness (0.063) despite owning little logic: cut it and the whole UI
disconnects.

`ChatManager` is the second-level hub, exposing `mainModel`, `archivedModel`,
`folderModels`, `folderModel`, `selectedChat`, `chatInfo`, `messageModel`, `title`
and `status`.

Two instantiable QML types are registered (`main.cpp:59-60`) — `LottieAnimation`
and `QrCode` — plus 33 uncreatable types so QML can hold typed pointers to
`Message*`, `Chat*`, the `MessageContent` hierarchy and friends.

### QML page flow

```
main.qml (PageStackWindow)
└── MainPage                       initialPage; calls appManager.initialize()
    ├── AuthenticationPage         if not authorised
    │   └── Loader → SignInPage / CodeEnterPage / PasswordPage / QrCodePage
    └── ChatsPage                  on appInitialized + authorised
        ├── ChatListView → ChatItem      (main list, and one per folder tab)
        ├── ArchivedChatPage
        ├── SettingsPage → LanguageSettingsPage
        └── ChatPage → MessageDelegate → MessageBubble / ServiceMessageDelegate
```

All page loading is synchronous `Qt.createComponent` — QML1 has no incubator.

---

## Threading

Only one thread besides the GUI thread: TDLib's receive loop, a `std::jthread`
started in `Client::initialize`, calling `receive(30.0)` in a loop.

Two things follow, and both are easy to get wrong:

- **`result()` is a queued signal.** Receivers run on the main thread. Safe.
- **`Client::send` callbacks are *not*.** The completion lambda passed to `send()` is
  invoked directly on the worker thread. Anything touching Qt objects or model state
  from there must marshal back — `ChatModel::requestMoreChats` does this via a queued
  `QMetaObject::invokeMethod`. Four other call sites still run model code on the
  worker thread: `AppManager.cpp:342` and `:360`, `LanguagePackInfoModel.cpp:79`,
  `MessageModel.cpp:194`.

Image decoding and `LottieAnimation` rasterisation both happen on the GUI thread.

---

## Component reference

| File | Role |
|---|---|
| `Client` | TDLib transport; worker thread; request/callback map |
| `StorageManager` | Entity cache + update fan-out (see above) |
| `AppManager` | Composition root; auth/connection state; the QML door |
| `Authorization` | Login state machine |
| `ChatManager` | Owns chat list models; formats the open chat's title/status |
| `ChatModel` | One Telegram chat list. Sorted by position, paged, demand-loads from TDLib |
| `ChatFolderModel` | The folder tab strip |
| `MessageModel` | Messages of the open chat; sorted insert, block merge |
| `Chat`, `User`, `BasicGroup`, `Supergroup`, `SupergroupFullInfo`, `File`, `Message` | Passive `QObject` value wrappers over `td_api` objects |
| `MessageContent` | Polymorphic message-body hierarchy (text, photo, audio, poll, …) |
| `MessageService` | Service-message payloads (joins, title changes, …) |
| `Utils` | Static formatters, exposed to QML as `utils` |
| `Locale` | `QTranslator` subclass; **every `tr()` in the process** routes through `getString` |
| `PluralRules` | Per-language plural forms |
| `Settings` | `QSettings` wrapper |
| `Emoji` | 3,773-entry static table backing emoji substitution |
| `NotificationManager` | Harmattan banners over D-Bus; owns the tap-to-open service and object paths |
| `ChatPhotoProvider` | `image://chatPhoto/` — decodes avatars at the requested size, crops and masks them, with a cache |
| `StickerProvider` | `image://sticker/` — decodes WebP stickers via libwebp, scaled during decode |
| `LottieAnimation` | rlottie renderer; auth pages only, never in a list |
| `QrCodeItem` | QR rendering for login-by-QR |
| `ScopeTimer` | Opt-in profiling (`-DMEEGRAM_PROFILE=ON`) |

### One File object per file id

`StorageManager::registerFile()` publishes a `File` as the canonical instance for its
id and **returns whichever instance is canonical** — which may not be the one passed in.
Anything that builds its own `File` from an embedded `td_api::file` has to adopt the
result: `Chat` for its photo, `MessagePhoto` and `MessageSticker` via
`MessageModel::linkContentFile`.

This is not decoration. `updateFile` only ever mutates the instance in the map, so a
second `File` for the same id never learns the download finished — which is exactly why
avatars sat on the placeholder forever. First registrant wins, so the same photo
appearing twice in a chat cannot orphan one of them.

`linkContentFile` is invoked **queued** from the `getChatHistory` callback, because that
callback runs on the TDLib worker thread and the map is also mutated from the main
thread on every `updateFile`.

### Value objects notify coarsely

`Chat` has **11 `Q_PROPERTY`s that all share one `chatChanged` signal**, emitted
from 11 sites in `Chat.cpp`. Qt invalidates every binding on all 11 properties per
emit, so a read-receipt update re-evaluates `title`, `photo` and `lastMessage`
bindings too. Splitting them into per-property notifiers is the obvious win if list
scrolling needs more headroom.

---

## What the knowledge graph showed

`graphify-out/` holds a 1,174-node / 1,519-edge graph over `src/` and the docs,
clustered into 90 communities. Two things it surfaced that reading alone did not:

**The real hubs, by betweenness** — `StorageManager` 0.105, `Chat` 0.084,
`AppManager` 0.063. Betweenness (paths through a node) turned out to be far more
informative than degree here.

**Degree is misleading in this codebase.** `Chat` and `AppManager` rank near the top
by edge count, but ~39 of their edges are `defines` — members, `Q_PROPERTY`s,
signals. The AST extractor makes any wide class look like a hub. `Chat` is a passive
value object with no outbound dependencies beyond `QObject` and composition; its
apparent centrality is an artifact. `StorageManager`'s is not: its edges are
genuine imports, calls and signal connections crossing module boundaries.

The graph is regenerated with `/graphify` and is not tracked in git.

---

## Oddities worth knowing

- **`ChatModel` was write-once.** Until recently a chat arriving after `populate()`
  could never enter the list, which forced startup to load *every* chat before
  showing anything. Chats are now ingested incrementally, and `loadChats` is driven
  by `fetchMore()` instead of a timer that ran until TDLib returned 404.
- **Emoji are images, not glyphs.** `Utils::replaceEmoji` rewrites emoji into
  `<img src=":/emoji/....png">` and the delegate renders `Text.RichText`. That means
  a `QTextDocument` per message and a PNG decode per emoji. 3,770 emoji PNGs
  (~20.5 MB) are compiled into the binary across six `.qrc` files.
  `main.cpp` also loads a `NotoEmoji` font that is in neither the qrc nor on disk —
  a silent no-op, kept only as a reminder that the font path was considered.
- **`Utils::getContent` and friends take `shared_ptr` by value**, so each formatter
  call costs several atomic refcount operations. Noticeable on an in-order Cortex-A8.
- **`debian/changelog` is stale** relative to `tools/setup-dependencies.sh` — it
  cites OpenSSL 1.1.1w and TDLib 1.8.35; the script now pins OpenSSL 3.5.7 and a
  TDLib commit. Trust the script.
