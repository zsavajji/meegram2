# MeeGram — agent context

Telegram client for the Nokia N9 (MeeGo 1.2 Harmattan). Read this first; it replaces
reading the tree. Written 2026-09-19 against HEAD `bc03881` (version 0.3.8).

The long-form reasoning lives in `docs/` and is more complete than this file. The N9
platform reference lives outside the repo at `../harmattan-basic-docs/AGENTS/` (load its
README plus the task-specific file before any QML, C++/platform or packaging change).

On branch `feature/optimization`: `PERF-TEST-PLAN.md` at the repo root is the validation
runbook for that branch's ten performance changes. If you are the agent with the Linux
build box and the device, start there.

## Stack and constraints

- Device: 1 GHz Cortex-A8, SGX530, 1 GB RAM, 854×480, single core. No debugger, no
  `ptrace`, `invoker` discards stderr.
- Qt **4.7.4**, QtDeclarative (**QML1**), `com.nokia.meego 1.1`, `com.nokia.extras 1.1`.
  No QtQuick 2, no `QtQuick.Controls`, no `QtGraphicalEffects`, no `Qt.binding()`,
  pre-ES5 JS (`var` + `function` only).
- C++ is **C++23** (`std::jthread`, `std::ranges`, `std::to_array`), built with a
  purpose-built GCC 14 cross toolchain, `-static-libstdc++`, hard float, NEON.
- TDLib 1.8.66 (pinned commit), rlottie, libwebp (decoder only), libogg + libopus
  (fixed point), OpenSSL 3.5 shipped in `/opt/meegram/lib`.
- Two processes on device: `meegramd` (owns TDLib, no Qt, ~24 MiB) and `meegram`
  (Qt/QML UI, ~78 MiB). Selected by `-DMEEGRAM_JSON_TRANSPORT=ON`; `OFF` builds the
  older single-process app and both stay buildable for bisecting.

## Working rules for this repo

- **Do not build, package or commit on the user's behalf.** Edits happen on macOS; the
  cross-build runs on a Linux box into `build-app/`. `build/` belongs to
  `tools/setup-dependencies.sh` (zlib, OpenSSL, TDLib, rlottie, webp, ogg, opus trees
  and their skip-stamps). Verify C++ with a standalone `g++` compile of the logic where
  possible and say plainly what was not verified.
- Comments in this codebase explain *why*, often with the measured number and the bug
  that motivated the line. Keep that style. clang-format: Google base, 160 columns,
  braces on their own line, 4-space indent.
- `-Wall -Wextra -pedantic`; Debug adds `-Werror`. Release defines `QT_NO_DEBUG_OUTPUT`,
  so use `qWarning` for anything that must reach the device log.
- Deliberate ceilings are marked `ponytail:` in a comment naming the upgrade path.
  There are 39 of them; `grep -rn "ponytail:" src resources/qml` is the debt ledger.
- Language-pack strings: `qsTr("Key")` resolves through `Locale::getString`; a key the
  pack lacks renders **as the key itself** with nothing in a release log. Verify new keys
  against Telegram's Android pack on device. The on-disk cache is UTF-16, so `grep` finds
  nothing in it.

## Hard rules (each one has cost a build cycle)

1. **Ids cross the QML boundary as decimal strings, both directions.** QML1 boxes any
   number outside int32 as a double and `QVariant(double).toLongLong()` corrupts the low
   bits. Every `Q_INVOKABLE`/slot takes `const QString &` and converts with `toId()`
   (`src/Common.hpp`); signals carry ids as `QString`; QML holds ids in
   `property variant`. `Q_PROPERTY(qlonglong)` exists only on `Chat::id`, `Message::id`,
   `User::id`. Never add another. Same rule for `File::size` (pre-formatted string).
2. **No default arguments on `Q_INVOKABLE`s.** moc emits a cloned metamethod and QML1
   mis-resolves the arity (`replaceEmoji` / `replaceEmojiSized`).
3. **`Client::send` callbacks run on the reader thread.** Touch nothing but the response;
   hop to the GUI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`,
   passing `td_api` objects as `void *` (`response.release()`). Capture an
   `alive = m_alive` `shared_ptr<atomic_bool>` cleared in the destructor.
4. **`Client::result()` is emitted on the GUI thread from `Client::dispatch`, which
   frees the object when the last subscriber returns.** Move fields out in the slot;
   never retain the raw `td_api::Object *`. `injectUpdate` goes through the same call.
5. **Never destroy an object QML is bound to synchronously.** Release and `deleteLater()`
   (`ChatManager::popContext`, `updateFolderModels`). Contexts are parented to
   `ChatManager`, never left to the QML collector.
6. **One `File` per file id.** `StorageManager::registerFile` returns the canonical
   instance; anything building a `File` from an embedded `td_api::file` must `adopt*`
   the result (`Chat`, `User`, every `Message*` content via `linkContentFile`).
7. **A local path is not a finished file.** Test `isDownloadingCompleted`, never
   `localPath !== ""`.
8. **`chatUpdated` means "look again", not "a message arrived".** It fires from eleven
   updates; `chatLastMessageChanged` is the narrow one. Store membership ≠ list
   membership: consult `Chat::positions()`.
9. **`Chat::setPositions` treats an empty vector as "unchanged".** Membership is presence
   of a position; order 0 is *not* absence.
10. **Under the daemon transport never send `close` on quit** (it would close the
    daemon's TDLib). `AppManager::close` already guards this.
11. **On device: `killall`, never busybox `pkill -f`** (matches nothing, exits 0, leaves
    stacked instances sharing one daemon → empty chat list that looks like a TDLib bug).
12. QML1 traps: `property variant x: null` reads back `undefined` (use
    `property QtObject`); an element read straight off a variant list is an unwrapped
    QVariant (copy it into a `property variant` first); `Connections` on a nullable target
    needs `target: x || null` + `ignoreUnknownSignals: true`; `opacity: 0` still
    hit-tests; `clip: true` on every list; a `Repeater` shadows `model`; rich text
    ignores `elide` (use `utils.elideEmoji`); `QList` has no initializer-list ctor.
13. Every segfault so far was object lifetime or a cross-thread model mutation, and each
    fired only after another fix made the path run. Suspect lifetime before logic.

## Architecture in one screen

```
meegramd (src/daemon/)                       meegram (src/)
  td_json_client ── receive thread ──┐         Client (ClientProxy.cpp) ── reader jthread
  broadcast every line to every UI   │  unix     │ posts dispatch(Object*) → emit result
  broadcastSplit: getCurrentState    │ socket    ▼
    → one update per line + "ok"     ├────────┬──────────┬──────────────┬──────────
  Notifier: updateNotificationGroup  │  StorageManager  AppManager   Authorization  MessageModel
    → MNotificationManager (libdbus) │  (entity cache,  (auth/conn   (login state  (one per open chat)
  poll loop: accept, peer check,     │   11 signals)     state, root)  machine)
    requests → td_send, tap handler  │        │
  D-Bus name com.meegram.Daemon      │   ChatModel ×N ── ChatManager ── ChatContext stack
                                     │        └──────────────┴──────────────► QML via `appManager`
```

- **QML sees three context properties**: `appManager` (the only door), `utils` (static
  formatters), `AppVersion`; plus image providers `chatPhoto` (decode at size, crop,
  mask, 256-entry RAM cache + raw ARGB disk cache in `~/.meegram/avatars`) and
  `sticker` (libwebp, scaled decode, 8 MB byte-budget cache; also video/GIF stills).
- **`AppManager`** builds `Client`, `Authorization`, `Locale`, `Settings`,
  `StorageManager` in its constructor; `ChatManager` only on `authorizationStateReady`.
  Owns `NotificationEndpoint` (daemon build) or `NotificationManager` (in-process build).
- **`ChatManager`** owns main/archive/folder `ChatModel`s, `ChatFolderModel`,
  `SearchModel`, and a stack of `ChatContext` (chat + `ChatInfoFormatter` + optional
  `MessageModel`). `pushChat`/`pushProfile` hand a page its own context; the page
  returns it via `popContext(token)` in `Component.onDestruction`. The topmost context
  with a message model is the open chat; `setOpenChat` sends `closeChat`/`openChat` and
  the app event filter re-closes/reopens it on minimise/restore.
- **`ChatModel`**: sorted by position order (decorate-sort-undecorate, 500 ms coalescing
  timer, skips `layoutChanged` when order is unchanged), `chatId→row` index, per-row
  `FormattedRow` cache, demand paging via `loadMore()` from `onAtYEndChanged`, title
  filter that pulls the whole list while set.
- **`MessageModel`**: ids sorted ascending, `FormattedRow` cache, album grouping
  (`messageAlbum`/`messageAlbumChild` content types), `opensRun` for the flat layout,
  send-state role from `lastReadOutboxMessageId`, reactions role, retries short/empty
  `getChatHistory` answers (`MinLoadedMessages` = 10, 3 retries), swaps the temporary id
  on `updateMessageSendSucceeded`, ignores `from_cache` deletions.
- **`Locale`** is a `QTranslator` backing every `tr()`; loads a disk cache of the pack
  before the QML scene is built (QML1 never retranslates); memoises `getString`.
- **Threads**: GUI + one reader thread per `Client`; daemon has poll loop + receive
  thread. Image decode and rlottie run on the GUI thread.

### File → role

| File | Role |
|---|---|
| `src/main.cpp` | Type registration, GL viewport, `MEEGRAM_HEADLESS`/`KEEPALIVE`/`QML_BENCH` switches, startup markers |
| `src/Client.hpp` + `Client.cpp` / `ClientProxy.cpp` | One surface, two transports; `@extra` is `"<pid>-<n>"` so several UIs share one daemon |
| `src/JsonCodec.*` + `tools/generate_json_client.cpp` | Client-direction TDLib JSON codec TDLib does not ship |
| `src/AppManager.*` | Startup, stall deadline (8 s), retry, daemon reconnect budget (5 × 2 s per minute), language pack refresh (2 days), `getCurrentState` replay, held notification tap |
| `src/StorageManager.*` | Entity cache + fan-out; `registerFile`; `loadUserFullInfo` |
| `src/ChatManager.*` | `ChatInfoFormatter`, `ChatContext`, `ChatManager`; `openProfile`, `fetchChat`, `createGroup`, `searchMentions` |
| `src/ChatModel.*`, `MessageModel.*`, `SearchModel.*`, `ChatFolderModel.*` | List models (see above) |
| `src/Chat.*`, `Message.*`, `MessageContent.*`, `MessageService.*`, `User.*`, `BasicGroup.*`, `Supergroup*.*`, `File.*`, `ChatPosition.*` | Passive value wrappers over `td_api` |
| `src/Utils.*` | Formatters, emoji table (`replaceEmoji`, `elideEmoji`, `emojiOnlySize`), save/open helpers |
| `src/Localization.*`, `PluralRules.*`, `LanguagePackInfoModel.*` | Translation |
| `src/NotificationManager.*` | In-process notifier (transport OFF only) |
| `src/NotificationEndpoint.*` | `com.meegram /notification openChat(QString)` (transport ON) |
| `src/daemon/main.cpp`, `Notifier.*` | Relay + notifier |
| `src/VoiceNote.*`, `OggOpus.*` | QAudioInput/Output + hand-muxed OggOpus (Qt-free below the API) |
| `src/LottieAnimation.*`, `QrCodeItem.*`, `ChatPhotoProvider.*`, `StickerProvider.*` | Rendering items/providers |
| `src/Log.hpp`, `ScopeTimer.hpp` | stderr → `~/.meegram/<bin>.log` (256 KiB, one rotation); opt-in `MEEGRAM_SCOPE`/`MEEGRAM_MARK` |
| `src/Emoji.cpp` | 3773-entry static table + `quickReactions()` (generated, do not read) |

### QML

```
main.qml (PageStackWindow; theme colours, isOnBubble(), chatPageComponent, openChat/openProfilePage/openPhoto, save plumbing)
└── MainPage         root, Loader: spinner | unreachable | welcome | ChatListView | tabs of ChatListView
    ├── AuthenticationPage   a sheet, never on the stack → SignIn/CodeEnter/Password/QrCode/SignUp
    ├── ChatPage        header, ListView<MessageDelegate>, composer, EmojiPicker, mention panel,
    │                   ContextMenu, reaction grid, VoiceNote, kept-alive Photo/File pickers
    │   └── MessageDelegate → MessageBubble (chrome) | ServiceMessageDelegate
    ├── ProfilePage, NewChatPage, NewGroupPage, ArchivedChatPage, SettingsPage, LanguageSettingsPage,
    │   PhotoViewPage (pinch zoom via resizeContent, swaps to the original when downloaded)
    └── components/: ChatItem, ListItem, TopBar, Icons (fontello), MyCountBubble, UIConstants.js
```

- Bubble vs flat layout: `model.isOutgoing` decides the side in both layouts;
  `appWindow.isOnBubble(model.isOutgoing)` decides colour, because white reads only on
  the accent balloon. The balloon is a `Rectangle { radius: 13 }` since 0.3.8, no PNGs;
  swipe **left** to reply.
- Dark theme: `theme.inverted` from `Settings::invertedTheme`; app colours are
  properties on `appWindow`; theme glyphs use `-inverse` assets via `themeIcon()`; the
  composer field background is regenerated by `tools/make_inverted_assets.py`.
- `ChatPage.qml` is compiled once and kept on `appWindow.chatPageComponent`; `MainPage`
  warms it 3 s after the chat list shows so the first tap does not pay the ~1 s compile.
- Delegates download on sight for photos, stickers, thumbnails, voice notes and
  avatars; documents and videos wait for a tap.

## Startup and the notification tap

1. `AppManager` ctor: loads the language-pack cache, seeds `signedOut` from
   `Settings::wasAuthorized`, builds `NotificationEndpoint` (so a tap that started the
   process is not lost; held in `m_pendingChatId` until `initialize()`).
2. `main.qml Component.onCompleted` → `appManager.initialize()`: options,
   `setTdlibParameters` ("Unexpected setTdlibParameters" counts as success),
   `getAuthorizationState` replayed through `injectUpdate`, 8 s stall deadline.
3. `authorizationStateReady` → `ChatManager` built → `chatManagerChanged` (releases a
   held tap so its `getChat` hits the socket first) → 250 ms later `getCurrentState`,
   which the daemon splits into per-line updates and answers with `ok`.
4. `appInitialized` = parameters accepted **and** language pack settled (cache hit,
   reply, or deadline). `MainPage` swaps its Loader on `initialized`/`signedOut`/`chatManager`.
5. Tap: banner action → `com.meegram.Daemon /chat/<id>` → daemon withdraws banner and
   calls `com.meegram /notification openChat` (auto-starts the app) → `AppManager`
   holds or emits `chatRequested` → `main.qml` pops to root and `openChat(chatId)`; if
   the chat is not in store, `fetchChat` injects the reply as `updateNewChat` and
   `chatAvailable` finishes the push.

Measured: warm-daemon tap-to-chat-page ≈ 4.2 s; first `getChatHistory` is the largest
remaining and unstable number (0.9–9.8 s). See `docs/notification-startup.md`.

## The daemon

- Socket `$XDG_RUNTIME_DIR/meegram.sock`, else `~/.meegram/sock`, mode 0600. Bus name
  claimed **before** the socket is bound (that is the mutex); the UI polls 3 s for the
  socket after activation.
- Peer check ladder in `isPeerTrusted`: same uid → aegis credential `meegram::Client`
  (`creds_getpeer`) → `/proc/<pid>/exe` equals the sibling `meegram` binary → uid alone
  if `/proc` is unreadable (stock devices make credentialed processes undumpable).
  `--trust-any-peer` for driving it by hand; deliberately a flag, not an env var.
- Non-blocking writes with a per-connection outgoing queue (8 MiB cap) and a wakeup pipe;
  this is a deadlock fix, not an optimisation.
- `nudgeIfOffline`: `setNetworkType` every 30 s while TDLib reports not connected, because
  nothing else clears TDLib's backoff. TDLib log verbosity is set to 1 (default wrote
  10.9 MB per cold start).
- Notifier: turns notifications on with `notification_group_count_max = 5`, keeps chat
  titles / user names / photo paths from relayed updates, suppresses the chat the UI has
  `openChat`ed, posts through `com.meego.core.MNotificationManager` with event type
  `x-nokia.messaging.im`, avatar as a plain path only when the download completed,
  retries without the image on refusal, latches the notification user id on success only.
- `debian/postinst` runs `killall meegram meegramd` so an upgrade replaces the resident
  binary; the UI has an automatic reconnect for exactly that.

## Build, package, device

```sh
export QT_SDK_PATH=... TOOLCHAIN_PREFIX=arm-none-linux-gnueabi
export PATH="$HOME/cross/arm-harmattan/bin:$QT_SDK_PATH/Madde/bin:$PATH"
./tools/build-toolchain.sh                         # once
./tools/setup-dependencies.sh harmattan "$QT_SDK_PATH"   # deps + td/ patches + JSON codec
cmake -B build-app -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake -DQT_SDK_PATH="$QT_SDK_PATH" \
      -DBUILD_HARMATTAN=ON -DMEEGRAM_JSON_TRANSPORT=ON -DMEEGRAM_PROFILE=OFF
cmake --build build-app -j4
cmake --build build-app --target package            # needs `mad set <target>`; does not compile
```

- Flags are sticky in the CMake cache; pass the full list every configure.
- Other switches: `MEEGRAM_PROFILE`, `MEEGRAM_GL_VIEWPORT` (ON; costs 9.1 MiB RSS, frame
  benefit unmeasured), `MEEGRAM_FILE_LOG` (ON), `MEEGRAM_JSON_BENCH`.
- Self-checks, all `EXCLUDE_FROM_ALL`, all need `-UNDEBUG`: `opus_roundtrip` (Qt-free),
  `json_roundtrip` (64-bit ids survive the codec), `notifier_check` (banner text from
  real update lines). `tools/emoji_reaction_check.cpp` builds with host g++ + QtCore.
  `tools/daemon_probe.c` is a static socket probe for the device.
- Install layout: `/opt/meegram/bin/{meegram,meegramd}`, `/opt/meegram/lib/{libssl,libcrypto}.so.3`
  + `libQtMultimedia.so.4`, `/opt/meegram/share/meegram-splash.png`,
  `/usr/share/dbus-1/services/com.meegram{,.Daemon}.service`, desktop file, 80×80 icon.
- Aegis (`debian/meegram.aegis`): provides `Client`; requests `meegram::Client`,
  `Location`, `TrackerReadAccess`, `TrackerWriteAccess`, `GRP::video` for the UI binary
  and the applauncherd launcher line. Missing credentials fail silently; open-mode dev
  phones enforce nothing; run `accli -I` on both devices first.
- Logs: `~/.meegram/meegram.log` and `~/.meegram/meegramd.log` on device. Repro a
  notification tap with `dbus-send --session --dest=com.meegram /notification
  com.meegram.Notification.openChat string:<chatId>`; wait for load average < 0.5 after
  a kill (applauncherd prestarts other apps and skews every number 10×).
- Env switches: `MEEGRAM_HEADLESS=1` (sync without a scene), `MEEGRAM_KEEPALIVE=1`
  (teardown experiment, measured useless), `MEEGRAM_QML_BENCH=N` (profile build only).

## Status

**Done** (see `docs/features.md` for detail): daemon transport and D-Bus activation;
daemon-side notifications with tap-to-open; per-page `ChatContext`; chat list with
folders, archive, search, pin/mute/read/delete; messages with entities, emoji images,
big emoji, replies with jump-to-quote, edit, delete (for me / for all), read receipts,
delivery ticks, reactions (standard twelve), @mention autocomplete; photos (send, albums
up to 10, pinch zoom, save original), stickers (tgs via rlottie, webp via libwebp, webm
falls back to emoji), voice notes recorded and played in-process, documents and audio as
file rows, video/GIF bubbles that hand off to the platform player; new chat, new group,
phone-contact import, profile page with members; dark theme; flat (no-bubble) layout;
measured profiling with the two real wins (avatar cache 64→256, `replaceEmojiSized`
memo) applied.

**Not done**: `Authorization` still runs in the UI (a first login needs the app open);
forwarding; message search; location/contact/poll delegates; inline video; download
progress (spinner only); custom-emoji and paid reactions; sending your own typing
action; recents/skin tones in the emoji picker; the daemon posts blocking D-Bus from its
receive thread; the `getCurrentState` replay still grows with the account
(demand-loading the chat list via `fetchChat` is the planned fix).

**Housekeeping**: "Show bubbles" is the one untranslated string by design.
`docs/architecture.md`'s notification data-flow section describes the transport-OFF
path and says so.

## Where to read more

| Question | File |
|---|---|
| How pieces connect, threading, the id invariant | `docs/architecture.md` |
| Why the daemon exists, memory measurements, JSON codec, what shipped | `docs/restructuring.md` |
| Why a tapped notification took 27 s and what fixed it; device repro | `docs/notification-startup.md` |
| Every feature's behaviour and its ceilings | `docs/features.md` |
| Build errors, device traps, past wrong diagnoses | `docs/troubleshooting.md` |
| Toolchain, dependencies, patches applied to `td/`, packaging | `docs/building.md` |
| Scroll-path profiling numbers and verdicts | `docs/profiling.md` |
| N9 platform rules, theme graphics, UX guidelines, aegis, device workflow | `../harmattan-basic-docs/AGENTS/` |
