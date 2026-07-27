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

## Data flow: one notification

`NotificationManager` never touches QML and never touches `Client`. It is a
`StorageManager` consumer that talks straight out over D-Bus, which is why banners
keep working with no UI on screen — and why it is the piece that most wants to
outlive the UI process.

1. `chatLastMessageChanged` fires (only from `updateChatLastMessage`).
2. `evaluateChat` walks its guards in order. Everything before the `mayPublish` gate
   may only take a banner **down**; everything after may raise one:

   ```
   chat exists → message exists, not outgoing, not service
   → already read?        withdraw, return      ← also reached via chatUpdated
   → !mayPublish          return                ← plain chat update stops here
   → positions() empty    return                ← in the store, not in a list
   → <= high-water        return                ← never notify backwards
   → dedupe / active+foreground / muted / older than app start
   → publish
   ```
3. `publish` resolves a notification user id over D-Bus, registers `com.meegram` and
   an object path `/chat/<id>` (`n` prefix for negative ids), then calls the platform
   notification manager. The object path *is* the chat identity — tap-to-open reads it
   back in `activate()` rather than trusting an action argument.

Two latch flags guard startup races, and both must latch **on success only**:
`m_userIdResolved` and `m_serviceRegistered`. Setting either before the attempt means
one early failure — the notification daemon not being up yet when the first message
arrives, which at startup it may well not be — disables the feature for the whole
session. That bug has been written twice.

`m_startedAt` filters the launch replay but goes dead once the process has been up a
while, which here is the normal case. `m_highWaterMessageId` is the durable guard:
unlike `m_notifiedMessageId` it survives a `withdraw`, so a re-delivered or
out-of-order update cannot raise a banner for a message a chat has already passed.

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
| `chatUpdated` | 11 sites | `ChatModel.cpp:29`, `NotificationManager.cpp:117` |
| `chatLastMessageChanged` | 1 site | `NotificationManager.cpp:118` |
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

Only 8 of 12 do any work. The last two are declared but never emitted *and* never
consumed — dead API surface.

### `chatUpdated` is "look again", not "something happened"

The name reads like an event. It is not: it fires from eleven different `td_api`
updates, of which exactly one — `updateChatLastMessage` — means a message arrived.
`updateNewChat` is the trap, because TDLib delivers a chat with `last_message_`
**already populated**, so a chat merely *arriving in the store* is byte-identical to
a message landing in it. Chat-list pagination delivers those in batches, which is
how scrolling came to raise notifications.

`chatLastMessageChanged` is the narrow signal for anything with a user-visible side
effect. `chatUpdated` remains correct for "re-read this chat", which is all
`ChatModel` ever wanted from it.

### Being in the store is not being subscribed

`StorageManager`'s maps are a cache of everything **TDLib has mentioned**, not of
what the user follows. A chat is admitted the moment it is referenced anywhere — the
origin of a forward, a search hit, the sender of a message in someone else's group —
and then receives updates like any other.

List membership lives in `Chat::positions()`, which stays empty for a store-only
chat. `ChatModel::getChatPosition` matches a position against its own `ChatList`;
`NotificationManager` requires the vector to be non-empty at all. Anything that acts
on "the user's chats" must consult positions rather than assuming a cache hit means
membership.

`Chat::setPositions` treats an **empty vector as "unchanged", not "remove from every
list"** — `updateChatLastMessage` and `updateChatDraftMessage` only populate
positions when they actually changed, and clearing on empty dropped every chat that
received a message out of the list. Positions are also written *before* the signal
is emitted, so a slot may read them as current for that update.

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

### Ids cross the boundary as decimal strings, in both directions

The single most load-bearing invariant in this codebase. QML1 boxes any JS number
that does not fit in int32 as a **double**, and the way back —
`QVariant(double).toLongLong()` — corrupts it. Supergroup chat ids and all message
ids (`id << 20`) are in the affected range; a private chat's id is a small positive
user id and is not, which is the entire "groups broken, private chats fine" pattern.

The rule, applied in both directions:

- **QML → C++**: every `Q_INVOKABLE` and public slot takes `const QString &`, never
  `qlonglong`. `toId()` in `src/Common.hpp` converts back. A JS number passed to a
  `QString` parameter converts via `QScriptValue::toString()`, which is exact, so
  call sites need no ceremony.
- **C++ → QML**: signals carrying an id declare `const QString &` too —
  `chatRequested`, `chatAvailable`. Neither has a C++ listener, so text costs nothing.
- **QML-side storage**: any property holding an id is `property variant`, never `int`
  or `real` — `ChatListView.qml:37`, `ChatPage.qml:18`, `:26`, `:27`, `:60`.

`Q_PROPERTY(qlonglong …)` is kept to the two ids QML genuinely reads, `Chat::id` and
`Message::id`. Getters that only C++ calls are deliberately **not** properties: a
property is QML surface, and unused surface is a landmine for the next binding
someone writes. Audit with:

```sh
grep -rn "Q_PROPERTY(qlonglong\|void .*(qlonglong" src/
```

then check each hit against what QML actually reads.

A related trap in the same family: a `Q_INVOKABLE` with a **default argument** makes
moc emit a cloned metamethod, so one name exists at two arities and QML1 must resolve
between them. Use two names instead.

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

## `Client` is the only door to TDLib

Worth stating on its own, because it is the narrowest interface in the codebase and
nothing else in `src/` touches `td::ClientManager`:

```
construction   AppManager.cpp:221                        — one site, one instance
outbound       send(td_api::Function, callback)          — ~30 call sites, 8 files
inbound        signals: result(td_api::Object *)         — 4 subscribers
```

That is a request/response pair plus a broadcast update stream, which is TDLib's own
shape preserved faithfully. Everything above it — `StorageManager`, the models, the
entity wrappers — depends on `Client` only through those two members.

The practical consequence is that TDLib can be relocated without touching its
consumers: see `restructuring.md`.

The lifetime detail that makes this work is `Client::disposeObject`. `result()` is
emitted queued, so the update object must outlive every subscriber's slot; a fifth
queued call frees it after the four `handleResult` calls drain. Any new subscriber
inherits that ordering guarantee for free, and any code that stashes the raw
`td_api::Object *` past its slot violates it.

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

`graphify-out/` holds a 1,357-node / 1,813-edge graph over `src/` and
`resources/qml/`, clustered into 94 communities. Two things it surfaced that reading
alone did not:

**The real hubs, by betweenness** — `Chat` 0.113, `StorageManager` 0.110,
`MessageModel` 0.094. Betweenness (paths through a node) turned out to be far more
informative than degree here.

**They are central for different reasons, and only one is real.** `StorageManager`'s
members sit in one community while its *signals* scatter across six — each signal is
clustered with its dominant consumer, which is what a correctly placed fan-out hub
looks like. `Chat`'s 42 members all sit in a single community; it scores high because
it is the shared vocabulary every subsystem names, not because it fans out. Cutting
`StorageManager` would sever the app; cutting `Chat` would only rename things.

**Degree is misleading in this codebase.** `Chat` and `AppManager` rank near the top
by edge count, but ~39 of their edges are `defines` — members, `Q_PROPERTY`s,
signals. The AST extractor makes any wide class look like a hub. `Chat` is a passive
value object with no outbound dependencies beyond `QObject` and composition; its
apparent centrality is an artifact. `StorageManager`'s is not: its edges are
genuine imports, calls and signal connections crossing module boundaries.

The graph is regenerated with `/graphify` and is not tracked in git. Two things that
silently corrupt a rebuild here, both worth re-applying by hand: `.qml` is not in
graphify's extension list, so the entire UI layer is dropped unless the QML files are
passed explicitly; and the LLM backend emits shorter node ids than the AST extractor
(`appmanager_appmanager` vs `src_appmanager_appmanager`), so semantic nodes ghost
alongside the AST ones instead of fusing unless the ids are normalised first. Both
failures report a healthy node count.

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
