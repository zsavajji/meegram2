# Troubleshooting

Errors this pipeline actually produces, what each means, and the rough edges
that are known but not yet fixed.

## Build errors

**`No device specified. Please specify 'harmattan' or 'simulator'.`**
`setup-dependencies.sh` takes the target as its first argument and the SDK root as
its second: `./tools/setup-dependencies.sh harmattan /path/to/QtSDK`.

**`TOOLCHAIN_PREFIX is not set` / `<prefix>-gcc is not on PATH`**
Export both before running:
`export PATH="$HOME/cross/arm-harmattan/bin:$PATH"` and
`export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi`.

**`No SDK path given`**
Without it TDLib's install step would target `/Madde/...` at the filesystem root —
and it is only read at the very end, after the whole build. Hence the up-front check.

**`Found relative path while evaluating include directories` (CMake, TDLib)**
Fixed. The CMake options were built as a string containing escaped quotes and then
expanded unquoted, so CMake received `-DOPENSSL_INCLUDE_DIR="/path"` with the quotes
inside the value. They are bash arrays now. If you see this, your `tools/` is stale.

**`Compatibility with CMake < 3.5 has been removed from CMake` (rlottie)**
rlottie still declares `cmake_minimum_required(VERSION 3.3)` and CMake 4.x dropped
support below 3.5. The script passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, which is
CMake's own documented escape hatch and also covers rlottie's bundled
freetype/pixman/stb subprojects. TDLib declares 3.10 and is unaffected. If you see
this, your `tools/` is stale.

**`Found neither 'TD_HAS_MMSG 1' nor 'TD_HAS_MMSG 0'`**
The `td/` checkout is reused across runs, so the patch has to be idempotent — this
error now means the macro genuinely moved upstream, not that it was already applied.
Check `td/tdutils/td/utils/port/config.h`. The patch disables `recvmmsg`/`sendmmsg`,
which the N9's kernel does not have.

**`Compiler exited 0 but produced no binary` / `Probe is not an ARM binary`**
The toolchain verification refuses to report success without a real ARM binary.
Usually means the wrong compiler is first on `PATH`.

---

**`mad: No '-t <target>' option given nor default target set`**
`CMakeLists.txt` calls `mad` without `-t`. Run `mad list`, then
`mad set <target>` once — the name matches a directory under `Madde/targets/`.

**`'class td::td_api::chatFolderInfo' has no member named 'title_'`**
TDLib API drift. `chatFolderInfo` used to carry `title:string`; it now holds
`name:chatFolderName`, which wraps a `formattedText`, so the title sits two levels
down. This class of break is worth expecting after any `TDLIB_COMMIT` bump — the
fix is always to read the current `td_api.tl` for the type in question.

**`libcrypto.so.3: cannot open shared object file` on the device**
The package is missing `/opt/meegram/lib`, or the binary lost its `RPATH`. Check
both with the two commands in [Verify before touching the
device](/building#verify-before-touching-the-device).

### Warnings that are safe to ignore

**`.dynsym local symbol at index N (>= sh_info of 3)`** on `libQtGui.so` /
`libQtOpenGL.so` — binutils 2.42 being strict about a malformed dynamic symbol
table in 2011-era Qt libraries. Unfixable short of rebuilding Qt, and harmless: the
device's own loader reads them fine.

**`function may return address of local variable`** in `sqlite3.c` — a long-standing
false positive in SQLite's `sqlite3SelectNew`, which copies the contents out before
returning. TDLib bundles SQLite unmodified.

**`Cannot generate a safe runtime search path … libz.so.1 … may be hidden by
build/zlib/lib`** — concerns the build-tree binary only. `INSTALL_RPATH` reduces the
installed binary's rpath to `/opt/meegram/lib`, so on the device `libz.so.1`
resolves from the platform. zlib's ABI is stable across 1.2.x/1.3.x. The underlying
cause is that `setup-dependencies.sh` leaves zlib and OpenSSL in `build/` rather
than installing them into the sysroot.

**`Manually-specified variables were not used: CMAKE_TOOLCHAIN_FILE`** — appears when
re-configuring an existing build directory. The toolchain file is read on the first
configure and stored in the cache; passing it again is redundant, not ignored.
Confirm with `grep cxx_compiler build-app/CMakeCache.txt`.

**changelog parse warnings / `unknown substitution variable ${shlibs:Depends}`** —
`debian/changelog` is missing blank-line separators between entries, and
`dh_shlibdeps` is commented out in `debian/rules`, so the package declares no
dependencies. Neither affects a hand-installed `.deb`.

## Traps this codebase has already fallen into

Each of these cost a build cycle. They are recorded because the symptom pointed
somewhere other than the cause.

### Only pinned chats appear

Two separate causes, a session apart.

`Chat::setPositions` treats an **empty** position vector as "unchanged", not "remove
from every list" — `updateChatLastMessage` sends an empty vector when positions did not
change, so clearing there dropped every chat that received a message. Pinned ones
survived because `updateChatPosition` re-asserted them.

Later, membership was tightened to require `order != 0`, on TDLib's documentation that
`0` means "not in this list". In practice only pinned chats arrive with a real order, so
that hid everything else. **Membership is presence of a position, nothing more.**

### Chats stop loading after the first page

`rowCount()` returns `m_count`, a reveal cursor — not `m_chats.size()`. The only thing
that grew it was `fetchMore()`, and **QML1's `ListView` never calls `fetchMore`**: that
is a Qt Widgets view API. So whatever had arrived by the first 500 ms sort was all that
was ever shown, which looked like "only pinned chats" and was flaky in exactly the way a
race is. The model now reveals everything it holds; paging is driven from
`onAtYEndChanged`.

### Opening a chat freezes the app

Re-positioning from `onContentHeightChanged` does not converge. Positioning builds
delegates whose real heights differ from `ListView`'s estimate for rows it has not built,
which moves `contentHeight`, which repositions, and with a `cacheBuffer` rebuilding rows
either side it never settles. Use a **bounded** retry.

### QML1 corrupts `qlonglong` method arguments {#qlonglong}

**The most expensive bug in this codebase's history.** It presented as "groups never
open", survived four wrong diagnoses, and was only found by logging the same id on both
sides of the call.

```
QML: "tap row 6 id=-1001383801308 type=4 title=Retro & Chill"
QML: "openChat requested for -1001383801308"
openChat: no chat in storage for id -1001383814072
```

```
sent ffffff16d8dfce24
got  ffffff16d8df9c48    ← top 48 bits intact, low 16 mangled
```

QtScript keeps a number that fits in **int32** as an immediate integer, so it marshals
through `QVariant(int)` and arrives exactly. Anything larger is boxed as a **double** and
converted with `QVariant(double).toLongLong()` — and that conversion arrives wrong. Every
supergroup chat id and **every message id** (message ids are `id << 20`) is in the
affected range; a private chat's id is a small positive user id and is not. That is the
whole "groups are broken, private chats are fine" pattern, and it silently corrupted
delete, edit and reply targets for as long as they have existed.

**Ids cross the QML boundary as decimal strings.** See `toId()` in `src/Common.hpp`. A JS
number passed to a `const QString &` parameter is converted by `QScriptValue::toString()`,
which is exact, and `toLongLong()` parses it with integer arithmetic — no float anywhere,
and no QML call site has to change. `main.cpp` carries a silent startup probe that fires
only if `double → qlonglong` is broken in the compiler output rather than only in QML's
marshalling; that would be a far wider problem than ids.

::: warning Never add a `qlonglong` parameter to a `Q_INVOKABLE` or public slot
It will work for every small id you test with and corrupt every large one.
:::

Two earlier diagnoses of "groups never load messages" were wrong but are worth keeping,
because both described real bugs that were fixed on the way:

- `loadMessages()` anchored on `lastReadInboxMessageId` with a **negative** offset. A
  chat never opened has unread messages but that pointer is still `0`, and
  `from_message_id = 0` means "from the newest" — asking for messages newer than the
  newest is rejected.
- `ChatManager::openChat` returned early and **silently** when the chat was not in
  `StorageManager`, while `main.qml` pushed `ChatPage` regardless. That left a page whose
  `chat`, `chatInfo` and `messageModel` were all undefined, so no `MessageModel` existed
  to request history — which is why grepping the log for `getChatHistory` found nothing
  and looked like the request was being rejected.

### `Q_INVOKABLE` with a default argument

moc emits a **cloned** metamethod for each default argument, so one name exists at two
arities and QML1 has to resolve between them. `replaceEmoji(text, size = 0)` was added
while one-argument calls were still live all over `MessageBubble`, and chat loading began
segfaulting. Use two names instead — `replaceEmoji` and `replaceEmojiSized`.

### A worker-thread callback outliving its object

`Client::send`'s completion handler runs on the **TDLib worker thread**, and capturing a
raw `this` is a use-after-free waiting for the object to be destroyed first. Leaving a
chat destroys `MessageModel` while a `getChatHistory` request is almost always still in
flight — segfault on chat exit. Capture a `shared_ptr<atomic_bool>` alongside and clear it
in the destructor; `ChatModel` and `MessageModel` both do this now.

This one stayed hidden until the [`qlonglong` fix](#qlonglong) made groups actually send
history requests. A latent crash needs the code path to work before it can fire.

### Tapping a notification lands on the startup spinner

**Resolved structurally — `MainPage` is now the chat list itself.**

`MainPage` used to be the root page *and* a permanent spinner, with `ChatsPage` pushed on
top of it from a one-shot `onAppInitialized`. The chat list sat at depth 2, so
`pageStack.pop(null, true)` — "pop down to the first page" — took `ChatsPage` with it and
left the app on a spinner nothing would ever resolve again. It was worked around with
`while (pageStack.depth > 2)` plus an `onStatusChanged` re-push guard.

`MainPage` now swaps its own `Loader` between spinner, welcome and chat list instead of
pushing anything, so the chat list is depth 1 and `pop(null, true)` is correct. Both
workarounds are gone. There is no longer a page under the chat list to strand the app on.

### A popped page closes the chat that replaced it

Tapping a notification runs `pageStack.pop(null, true)` and then `openChat(chatId)` in the
same turn. QML destroys the popped page **afterwards**, by which point the new chat is
already selected — and `ChatPage.chat` is a live binding to `chatManager.selectedChat`, so
the dying page's `Component.onDestruction` read the *new* chat's id and closed it. That
disposed the model the freshly pushed page was bound to, with no `selectedChatChanged` to
make QML re-read: a dangling model and a page in a state nothing could have navigated to.

Two fixes, either of which prevents it. `ChatPage` captures its chat id once in
`Component.onCompleted` — a declared initialiser would be a binding and would do the same
thing. And `closeChat` ignores a close for a chat that is not the selected one, which is
the guard at the point every caller funnels through.

### Destroying an object QML is still bound to

`ChatManager::openChat`/`closeChat` replaced `m_messageModel` and `m_infoFormatter` by
assignment, which destroys the previous ones **synchronously** — while `ChatPage`'s
`ListView` still had the old model, and with `closeChat` running *during* page teardown
when bindings are still firing. Release and `deleteLater()` instead, the same way
`updateFolderModels()` hands over its old models.

### The download completes and nothing updates

Two `File` objects for one file id. `updateFile` only mutates the one in
`StorageManager`'s map, so a `File` built separately from an embedded `td_api::file`
never learns the download finished. This hit avatars first, and would have hit photos and
stickers identically. See
[One File object per file id](/architecture#one-file-object-per-file-id).

### A local path is not a finished file

TDLib fills in `local.path` when a download **starts**. Testing `localPath !== ""`
points at a half-written file — a broken image in QML, a red square in a notification.
Test `isDownloadingCompleted`.

### QML `variant` initialised to null

`property variant x: null` reads back as `undefined`, so `x === null` is never true. The
photo picker was never constructed and `pageStack.push()` was handed `undefined`, which
does nothing and reports nothing. Use a typed `property QtObject`, or a falsy check.

### `QList` has no initializer-list constructor

That arrived in Qt 4.8; Harmattan is 4.7.4. `QVariantList` has to be built with
`operator<<`. `std::vector` is unaffected.

### Delete silently allocated gigabytes

`std::vector<int64_t>(messageId)` is the **count** constructor — a vector of `messageId`
zeroed elements, and message ids run into the billions. Wanted `{messageId}`.

### Nothing appears in the log at all

`qWarning` writes to **stderr**, and the `.desktop` launches through `invoker`, which
discards it — so `grep` over `/var/log/syslog` finds nothing no matter how much the app
is logging. Run the binary directly instead of tapping the icon:

```sh
ssh user@<n9-ip>
export DISPLAY=:0
/opt/meegram/bin/meegram 2>&1 | tee /home/user/meegram.log
```

An empty log then means the code path genuinely did not run, which is information. An
empty syslog means nothing at all.

### `debian/` changes have no effect on the package

`dpkg-buildpackage` runs with `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}`, so it reads
`build-app/debian/`, not the source tree's. That directory was copied once at configure
time behind an `if(NOT EXISTS)` guard, so a build tree kept the `debian/` of its
first-ever configure forever — editing `debian/changelog` did nothing and the package
kept coming out with the old version number. The `package` target now copies it on every
run. The same staleness applied to `debian/meegram.aegis`, so credential changes were
never in a package either.

### A null `MessageContent` crashes the preview

`Utils::getContent(MessageContent *, …)` casts and dereferences in every switch branch
and had no null guard, while `Message::content()` genuinely can be null —
`MessageModel::data` already tested for it. Reachable from the chat list's last-message
preview and from reply previews. Guarded once in the shared function rather than at each
caller. `Utils::getChatTitle` had the same hole: `StorageManager::chat()` returns null for
a chat it does not hold, which a reply forwarded from an unloaded chat reaches.

### `qDebug` vanishes in release

Non-debug builds define `QT_NO_DEBUG_OUTPUT`, so a failing TDLib request logged with
`qDebug` leaves no trace at all. Use `qWarning` for anything you would want on device;
it survives.

## Known rough edges

**Qt4 not found** — `Found unsuitable Qt version "" from NOTFOUND`.
`FindQt4` locates Qt by *running* qmake, and the sysroot's qmake is an ARM binary
that cannot execute on the build host. `tools/toolchain.cmake` resolves this by
globbing MADDE's host qmake at `$QT_SDK_PATH/Madde/targets/harmattan*/bin/qmake`,
which conveniently already reports sysroot-prefixed paths. If your SDK lays that
out differently, pass `-DQT_QMAKE_EXECUTABLE=` explicitly; verify with
`file <qmake>` that it is an x86 binary, not ARM.

**MADDE binaries are 32-bit x86.** On a 64-bit host:

```bash
sudo dpkg --add-architecture i386
sudo apt install libc6:i386 libstdc++6:i386 zlib1g:i386
```

**`debian/compat` is 7**, which modern debhelper treats as deprecated and may
refuse. Packaging normally runs inside MADDE's own environment, so host debhelper
may never be used — but if you see a compat error, bumping that file is the fix.

**A "slim" sysroot may lack development headers.** `tools/build-toolchain.sh`
checks for `usr/include/features.h` and stops early rather than failing deep into a
GCC build.

---

## Verification status

Confirmed against real artefacts:

- **Float ABI** — `readelf -A` on a real Harmattan sysroot's libc.
- **TDLib API compatibility** — all 221 `td_api` types/methods and all 132 distinct
  field accesses in `src/` exist in TDLib 1.8.66. Several signatures did change
  underneath (`sendMessage` gained `topic_id:MessageTopic`, `inputMessageText`
  replaced `disable_web_page_preview` with `link_preview_options`), but only in
  fields this code never sets. `inputMessageReplyToMessage` was the one genuinely stale
  call site; it has since been rewritten for the current signature and replies work.
- **`qlonglong` marshalling** — the [id corruption](#qlonglong) was confirmed on device by
  logging the same value on both sides of one call, not inferred.

::: info Not yet validated end to end
 there is no CI in this repository, and the pipeline
has not been run start-to-finish in a single clean pass. rlottie and the sysroot question above are the two places most likely to need
iteration.
:::
