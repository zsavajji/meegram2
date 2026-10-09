# Handoff: port MeeGram's UI to Qt 5 / Qt Quick 2 (qt5-harmattan)

Written 2026-10-09 for the agent that starts the port. The user is the author of both meegram2 and
qt5-harmattan and asked for this port: Qt 4.7's QML1 is now the bottleneck. On 2026-10-09 the chat
list and message rows went from QML delegates to painted C++ items and the user called the result
"night and day faster"; everything left on screen still goes through QGraphicsView. Qt Quick 2's
scene graph draws on the SGX530 with batching, which is the next step of the same kind.

qt5-harmattan's `SPECS.md` says "not porting MeeGram": that line predates this decision. Leave
qt5-harmattan's public files app-agnostic anyway (its rule: they name no specific app).

## Where you work

- **Build box** `zsavajji@192.168.1.12`. The port is a separate clone, `~/projects/meegram2-qt5`,
  of `~/projects/meegram2` (the user creates it, see "Before you start"). Work only in the clone
  and, for SDK additions, in `~/projects/qt5-harmattan` under its own rules (`HANDOFF.md` there).
  Never write to `~/projects/meegram2`: it is the shipping Qt 4 app and the user's working tree.
- **Phone** (`ssh -l user n9` from the box): copy files only into `/home/user` (`MyDocs` for
  `.deb`s; `/tmp` is a 4 MB tmpfs). Installs and anything as root are the user's: give the exact
  commands. Never busybox `pkill -f` (matches nothing, exits 0): `killall` or your own PIDs.
  The installed Qt 4 MeeGram must keep working next to the port.
- **Commits:** meegram2's rule is that agents never commit. Ask the user at the start whether
  that holds for the clone (a long port wants commits on a `qt5` branch there).

## Before you start (the user's steps)

1. Commit meegram2 on the box: the working tree holds the C++ rows work of 2026-10-09 plus six
   settings/clear-cache files edited on the box. A clone takes only committed history.
2. `git clone ~/projects/meegram2 ~/projects/meegram2-qt5` (reads the original, writes nothing
   there), then start the agent in `~/projects/meegram2-qt5`.

## Read first

- meegram2: `CLAUDE.md` (agent context, hard rules 1-15), `docs/architecture.md`,
  `docs/features.md`, `docs/troubleshooting.md`, `docs/building.md`.
- qt5-harmattan on the box: `HANDOFF.md`, `SPECS.md`, `docs/developing.md` (how an app is built,
  run and packaged: follow it), `PLAN.md` (private: facts and the SGX530 findings).
- `~/projects/n9browser/docs` (`device.md`, `gotchas.md`): SGX530/EGL gotchas, read-only.
- Harmattan platform reference: `harmattan-basic-docs/AGENTS/` (on the Mac; ask the user to copy
  it to the box if needed).

## What stays, what changes

**Untouched: `meegramd`** (`src/daemon/`, Qt-free, TDLib over a Unix socket, D-Bus activated as
`com.meegram.Daemon`, notifications). The Qt 5 UI talks to the *installed* daemon from the Qt 4
package: the daemon already serves several UIs (`@extra` is `"<pid>-<n>"`) and trusts a peer with
the same uid first. So the port is the UI process only, and `MEEGRAM_JSON_TRANSPORT=ON` is the only
transport (drop the in-process build in the clone).

**Mostly source-compatible (Qt Core / Gui / Network / D-Bus):** `Client`/`ClientProxy`,
`JsonCodec`, `StorageManager`, `ChatManager`, the models, the `td_api` wrappers, `Utils`,
`Localization` (a `QTranslator`), `Settings`, `AppManager`, `NotificationEndpoint` (Qt D-Bus is in
the runtime). Known Qt 4 → 5 edits: `roleNames()` is a virtual (add `override`; check nothing still
calls `setRoleNames`), `QTextCodec::setCodecForCStrings/Tr` are gone (UTF-8 is the default),
`Q_DECLARE_METATYPE`/`qRegisterMetaType` as before, string `SIGNAL/SLOT` connects still work.

**Qt 4 / QtMobility / MeeGo-only, to replace** (inventory 2026-10-09):

| Today | Qt 5 | Notes |
|---|---|---|
| `QDeclarativeView` + `QGLWidget` viewport, `MDeclarativeCache` booster, `invoker` in the `.desktop` | `QGuiApplication` + `QQuickView`, `showFullScreen()` | **Never launch through invoker/applauncherd** (they preload Qt 4 and the old libstdc++). Cold start ~2 s is the floor without a booster (qt5-harmattan task 3.5). |
| `import QtQuick 1.1`, `com.nokia.meego 1.1` (31 files, ~7,000 lines) | `QtQuick 2.x`, `com.nokia.meego 2.0` | The Qt 4 components' API on Qt Quick 2. Compile QML into the binary (`qtquick_compiler_add_resources` / `CONFIG += qtquickcompiler`), or the first start compiles and fsyncs it. |
| `com.nokia.extras 1.1` (`InfoBanner`, `DatePickerDialog`) | **not in the SDK** | No extras in qt5-harmattan's components source. Port the two to Qt Quick 2 as a generic `com.nokia.extras` module in qt5-harmattan (the user allows SDK additions), or as app components. |
| `QDeclarativeItem` C++ items: `ChatRow`, `MessageRow`, `MemberList`, `EmojiGrid` (`src/ListRow.*`, `PressItem`), `LottieAnimation`, `QrCodeItem` | see "The list delegates" below | `QQuickPaintedItem` rasterises on the CPU and uploads a texture per update; scene-graph items (`QSGNode`) don't. |
| `QDeclarativeImageProvider` (`ChatPhotoProvider`, `StickerProvider`) | `QQuickImageProvider` (or `QQuickAsyncImageProvider`) | WebP is a stock image plugin in the SDK: `StickerProvider` may become a plain `Image`. Keep the avatar mask + disk cache. |
| QtMobility Contacts (phone-contact import: `QContactManager`) | **none** (no qtpim) | Harmattan contacts live in Tracker: query it over D-Bus/SPARQL from C++, or port qtpim with a Tracker backend into qt5-harmattan (bigger). Ask the user. |
| `QtMobility.gallery` (`PhotoPickerPage`: `DocumentGalleryModel`) | **none** | Same choice: Tracker over D-Bus, a `FolderListModel` over `MyDocs` (Qt.labs.folderlistmodel is in the SDK), or port a docgallery module. |
| QtMultimediaKit `QCamera` + `QAbstractVideoSurface` (QR scanner, `src/QrScanner.*`) | Qt Multimedia 5 `Camera` | Works with Aegis `GRP::video` (zero-copy viewfinder). quirc needs luma: check whether a `QAbstractVideoFilter`/`QVideoProbe` can map frames on the zero-copy path. |
| `QAudioInput`/`QAudioOutput` (voice notes, `src/VoiceNote.*`, Qt 4's bundled `libQtMultimedia.so.4`) | Qt Multimedia 5, same classes | Backends present: `libqtmedia_pulse`, `libqtaudio_alsa`. |
| `Qt.labs.folderlistmodel 1.0` (file picker) | `Qt.labs.folderlistmodel 2.x` | In the SDK. |
| Aegis manifest (`debian/meegram.aegis`) | a manifest for the new binary path | `meegram::Client` (the daemon's peer check accepts same-uid anyway), `GRP::video` (camera), `TrackerReadAccess`/`TrackerWriteAccess` for contacts/gallery. |

## The list delegates (the part that matters for speed)

The Qt 4 build's speed came from replacing QML1 delegates with painted C++ rows. On Qt Quick 2 the
calculus is different: QML delegates are compiled, bound through V4 and drawn by the scene graph in
batches, while a painted item would be CPU-rasterised and re-uploaded. So:

1. **First** port the QML delegates, not the C++ rows. The last QML versions are in git at
   `78943ac` (`resources/qml/components/ChatItem.qml`, `MessageDelegate.qml`,
   `MessageBubble.qml`, `ServiceMessageDelegate.qml`, `MyCountBubble.qml`); the C++ rows
   (`src/ChatRow.cpp`, `src/MessageRow.cpp`) hold the same geometry, number for number, if you need
   the reference. Keep the models, the `FormattedRow` caches and the emoji memo.
2. Measure scrolling and chat-open time against the Qt 4 build on the same phone.
3. Only if a delegate is still the hot spot, write that one as a scene-graph `QQuickItem`.

## Hard rules that still apply on Qt 5

- **Ids cross into QML as strings** (rule 1): a JS number is a double in V4 too; 64-bit ids lose
  their low bits.
- **Gestures in item coordinates, never scene** (rule 15): check how `com.nokia.meego 2.0` rotates
  portrait before trusting `scenePos`.
- Object lifetime: QML must never hold a C++ object that gets destroyed synchronously (rules
  3-6, 13); `deleteLater`, contexts parented to `ChatManager`.
- Language-pack keys render as the key when missing (verify against the device's pack).
- Rules 2, 12 and 14 are QML1 / Qt 4 moc specific: re-check rather than assume.

## SGX530 / Qt 5 on Harmattan gotchas (from qt5-harmattan)

- No pbuffer EGL configs; NPOT textures only with CLAMP_TO_EDGE and no mipmaps; the GLSL compiler
  segfaults on dynamic indexing of uniform arrays.
- qtdeclarative `0003` (an FBO per depth/stencil buffer) and `0004` (layers at least 16×16) are
  needed for effects and layers: test any `layer.enabled`/`ShaderEffectSource` you add.
- The basic render loop only (the threaded one deadlocked; one core gains little).
- Binaries load `/opt/qt5/lib` through DT_RPATH; `LD_LIBRARY_PATH` does nothing; no `LD_PRELOAD`.
- Run from ssh in the session: `. /tmp/session_bus_address.user; DISPLAY=:0 <binary>` (the bus
  address is what reaches the keyboard).
- After deploying ~100 MB the phone flushes to flash for ~30 s: `sync` before timing anything.

## Build

- Toolchain and sysroot: qt5-harmattan's (`. ~/projects/qt5-harmattan/scripts/env.sh`,
  `cmake/harmattan-qt5.cmake`, `--sysroot=$WORK/sysroot`). It is GCC 14.2 like meegram2's, but
  against a different sysroot and with libstdc++ shared from `/opt/qt5/lib`.
- The UI links TDLib's `td_api` object code (`JsonCodec`, generated client). Rebuild the
  dependencies in the clone with the qt5 toolchain (`tools/setup-dependencies.sh` owns `build/`)
  rather than linking objects made for the static-libstdc++ build. TDLib with LTO needs a lot of
  RAM: compile wide, link `-j1`.
- Port `CMakeLists.txt` to `find_package(Qt5 ...)` + AUTOMOC + compiled QML; keep
  `MEEGRAM_PROFILE` (the `MEEGRAM_SCOPE` counters are how speed was measured so far).

## Package and install side by side

- Package `meegram-qt5`, binary under `/opt/meegram-qt5/bin/`, its own `.desktop` (`Exec` the
  binary directly, a distinct name such as "MeeGram (Qt 5)") and icon, `Depends: meegram (>= 0.4.5),
  qt5-harmattan, qt5-meego-components, qt5-meego-inputcontext` (the daemon comes from the Qt 4
  package). Build it as `docs/developing.md` step 7 says (gzip members, `digsigsums`, `_aegis`).
- **D-Bus name clash:** the Qt 4 UI owns `com.meegram` (`/notification openChat`, the notification
  tap). While both are installed, the Qt 5 UI must not claim it (use another name, or skip the
  endpoint) until it replaces the Qt 4 one.
- Land `.deb`s in `/home/user/MyDocs`; the user installs from `devel-su` (root has full
  capabilities on the 6.18 release since kernel `85f673cc2`; on older images `killall` must run
  as `user`).

## Order of work

0. Ask the user: commit policy in the clone; contacts and gallery approach; extras in the SDK or
   in the app.
1. Hello world: `QQuickView` + `com.nokia.meego 2.0` `PageStackWindow` from the clone's build, on
   the phone.
2. Core C++ on Qt 5 + a chat list (QML delegate) talking to the running daemon.
3. Chat page: message delegates, composer, emoji picker, swipe-to-reply, menus.
4. Media: photos, albums, stickers (Lottie, WebP), voice notes, video/document handoff.
5. Settings, account, sessions, sign-in (code, password, QR login), profile and members.
6. Platform: contacts import, gallery picker, QR scanner camera, Aegis manifest, package, side by
   side with the Qt 4 app.
7. Measure against the Qt 4 build (same phone, same account, screen on and unlocked): scroll frame
   pacing, `chatpage-compiled → chatpage-completed`, cold start, RSS. Write the numbers into the
   clone's `docs/profiling.md`.
