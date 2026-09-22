# Handoff: 0.4.5, built and never run

Written 2026-09-21. Everything in this release was written, compiled and packaged in one
session, and **none of it has executed** — not on the device, not anywhere. The `.deb` is
on the phone at `/home/user/MyDocs/meegram_0.4.5_armel.deb` and has not been installed.

Read `CLAUDE.md` first; this is the list of what to distrust in it.

## What is actually verified

| | |
|---|---|
| C++ compiles | Yes, both binaries, cross toolchain, `-Wall -Wextra -pedantic`, no warnings from any new file |
| C++ links | Yes — `meegram` and `meegramd` |
| Package builds | Yes, 26,587,820 bytes, `_aegis` attached, `RPATH=/opt/meegram/lib`, both binaries and the three bundled libraries inside |
| Transferred | Yes, md5 `8d2e42f6b62cc8d2d6e46f88a4fa2b5e` matches on both ends |
| **QML parses** | **No.** QML1 compiles at runtime; a typo in any of the four new pages is invisible until the page is opened |
| **Anything runs** | **No** |
| Measured | Nothing. Every number in this release's docs is what the *old* code cost |

A note on verifying the binary: **do not grep it for QML source text.** rcc stores these
files compressed, so neither the old nor the new text is in there as plain bytes, and a
zero match means nothing. Resource *names* are stored as UTF-16BE (`python3 -c "print(open('build-app/meegram','rb').read().count('SessionsPage.qml'.encode('utf-16-be')))"`),
which is the only string search worth running. Otherwise trust the mtime chain: source →
`build-app/meegram_autogen/*/qrc_qml.cpp` → `build-app/meegram` → the `.deb`.

## Debug in this order

The list is ordered by how likely each is to be wrong, not by importance.

### 1. Settings scrolls, and the pages below it exist

`SettingsPage.qml` outgrew its screen when it gained sections — about 1400px of rows in an
819px content area — and spent one build with no `Flickable` at all, so everything from the
Storage section down was unreachable. It is wrapped now. Check that Storage and Account are
reachable, then open each page it leads to: **Account**, **Devices**, **QR scanner**. First
run ever for all three; a property typo shows up as an empty page and a line in
`~/.meegram/meegram.log`.

### 2. The demand-loaded chat list

The largest architectural change here, and the one with real risk. See
`docs/notification-startup.md` and `docs/features.md` for the design. What to exercise:

- **First screen** appears at all, warm daemon and cold.
- **Scroll to the bottom** pages more in. `getChats` has no offset, so paging asks for a
  longer prefix each time and `StorageManager::fetchChat` skips what it already holds; if
  paging re-requests everything, that dedupe is broken.
- **A message in a chat below the loaded window** pulls that chat to the top. This path was
  dead before the fix in `StorageManager::handleResult` — `updateChatLastMessage` for an
  unknown chat used to fall off the end of an `if` — so if a chat *never* appears when
  someone writes, look there first.
- **Search** (the title filter) still pulls the whole list in.
- **The daemon's replay is filtered.** `broadcastSplit` drops `updateNewChat`,
  `updateChatLastMessage`, `updateChatPosition` and the three `*FullInfo` types from the
  `getCurrentState` answer only. The log line `meegramd: replayed N updates, M demand-loaded`
  says how many of each; if M is 0 the filter is not firing and the UI is paying for both
  mechanisms.

**If the chat list is empty**, before suspecting any of the above: check for stacked
instances. `killall`, never busybox `pkill -f`, which matches nothing and exits 0.

### 3. Logging out

`AppManager::logOut` wipes `~/.meegram/avatars`, then TDLib's `logOut` destroys the database
and closes the instance. Two consequences that are deliberate and that nothing else in this
codebase does:

- **`meegramd` exits** when it sees `authorizationStateClosed` (`_exit(0)`, 250 ms after
  relaying the line). It is D-Bus activated, so the UI's reconnect brings a fresh one up on
  `WaitTdlibParameters`. If the app hangs after a logout, look for a daemon that exited and
  was not re-activated, or one that did not exit and is sitting on a dead TDLib.
- **The session is cleared, not destroyed.** `AppManager::clearSession` runs on a
  `QTimer::singleShot(0)` after `signedOut` goes true; `main.qml` pops the stack in the same
  turn. The ordering is the whole design: delegates hold raw `Chat*` and `File*` owned by
  `StorageManager`, so the store may only drop its last reference once those pages are gone.
  **If there is a crash on sign-out, this is where it is** — and the fix is to delay the
  clear further, not to reorder it.

Then sign back in **without restarting** and confirm the chat list is the new account's.
That case is exactly what `clearSession` exists for; before it, `handleAuthorizationState`'s
`if (m_chatManager) return;` reused the previous account's models.

### 4. The QR scanner

`src/QrScanner.cpp`, `lib/quirc` (submodule, pinned at 927d680). Reached from Devices →
action menu → Link desktop device.

- **A black viewfinder** is the aegis question. The camera rides on `GRP::video`, already in
  `debian/meegram.aegis`, but **this device runs aegis in open mode and enforces nothing**,
  so a missing credential cannot reproduce here. `accli -I` on a closed-mode phone is the
  only real test.
- **A viewfinder that shows noise** means the frame format negotiated is not one
  `QrVideoSurface::present` converts. It advertises UYVY, YUYV, YV12, NV12, NV21, RGB32,
  ARGB32 and RGB24; anything else is refused outright rather than drawn wrong, so noise
  would mean a stride bug rather than a format one.
- **The viewfinder is grayscale on purpose.** quirc needs luma, and for every format the
  camera produces the luma is either a plane or every other byte, so one pass feeds both the
  decoder and the screen.
- **Decoding is throttled to 350 ms.** A code that takes a moment to read is expected; one
  that never reads is a bug.

### 5. Everything else

Account page fields commit on focus loss (`Account::setName` sends both names in one
request); the birthday picker; the sessions list and terminate; per-scope notification mute;
the storage size and Clear cache; skeuomorphic bubbles and whether the paddings land where
the drawn shape's edges are; animated stickers off falling back to the emoji; cancelling a
download on a document, a video and a voice note.

## Language-pack keys

About twenty new `qsTr` keys across the new pages: `FirstName`, `LastName`, `UserBio`,
`Username`, `Birthday`, `Set`, `LogOut`, `AreYouSureLogout`, `Devices`, `CurrentSession`,
`OtherSessions`, `TerminateSessionQuestion`, `TerminateAllSessions`, `AreYouSureSessions`,
`NoOtherSessions`, `LinkDesktopDevice`, `AuthAnotherClientScan`, `AuthAnotherClientNotQr`,
`AnimatedStickers`, `ShowSensitiveContent`, `NotificationsPrivateChats`,
`NotificationsGroups`, `NotificationsChannels`, `NotificationsAndSounds`, `Storage`,
`LocalDatabase`, `ClearCache`, `AreYouSureClearCache`, `General`, `Appearance`, `Account`,
`Profile`.

Some are real Telegram Android keys and some are guesses. **A key the pack lacks renders as
the key itself**, which is why the section headers were chosen to read as English words —
`Appearance` degrades to "Appearance". Verify by decoding the cache, not by grepping it: it
is UTF-16 and `grep` will find nothing either way.

Strings deliberately left untranslated, because Telegram has no key for them: "Show
bubbles", "Skeumorphic bubbles", "Remove birthday", "Not possible on this device, sorry!",
and the language-restart dialog.

## Traps this session hit, so nobody hits them twice

- **`QtMultimediaKit` is not in the `QtMobility` namespace.** It opens `QT_BEGIN_NAMESPACE`,
  while `QtContacts` right beside it opens `QTM_BEGIN_NAMESPACE`. Copying the contacts
  block's `QTM_USE_NAMESPACE` fails with "'QtMobility' is not a namespace-name".
- **`QSize::scaled` is Qt 5**; 4.7 has the mutating `QSize::scale`.
- **Two LTO links must not overlap.** `-j4` gets `collect2: ld terminated with signal 9`
  and make *deletes* the half-written `meegramd`. Compile wide, then `-j1` to link.
- **CMake needed `LANGUAGES CXX C`** once quirc arrived; without it the target has no link
  language and configure fails.
- **Member initialisation order.** `m_account` was written into `AppManager`'s initialiser
  list next to `m_authorization` while `m_storageManager` is declared later, so it would have
  been constructed with a null pointer. Members initialise in *declaration* order.

## Not built, deliberately

- **Changing the phone number.** Needs `sendPhoneNumberCode` → a code screen →
  `checkPhoneNumberCode`. The row says "Not possible on this device, sorry!".
- **LED, vibration and sound settings.** Per-scope mute is TDLib and is done; the rest is
  Harmattan's notification event type plus `com.nokia.profiled`, and the banners are posted
  by `meegramd`, which is Qt-free and has no `QSettings` — so the preference would have to
  cross into it by a file or a D-Bus method. Check first whether the profile is already
  respected; the platform may be doing it for free.
- **Live retranslation.** Changing the language warns that a restart is needed. QML1 never
  retranslates, so reloading the pack mid-session would leave built pages in the old
  language and give the new one only to pages pushed afterwards.
- **A cap on demand fetches.** `StorageManager::fetchChat` has no ceiling on how many can be
  in flight; marked `ponytail:` there.

## Where the work is

Four commits on top of `ac90ef1`, plus an uncommitted 0.4.5 bump, the settings scroll fix
and cancellable downloads:

```
abe1b8c  feat: improve chat initial load on demand
d3a1f00  feat: requested skeumorphic bubbles back
8e35e50  feat: sessions login, QR scan
9dab0fc  feat: locale change
```

New files: `src/Account.{hpp,cpp}`, `src/SessionModel.{hpp,cpp}`, `src/QrScanner.{hpp,cpp}`,
`cmake/quirc.cmake`, `lib/quirc` (submodule), `resources/qml/AccountSettingsPage.qml`,
`SessionsPage.qml`, `QrScannerPage.qml`, `components/SectionHeader.qml`.
