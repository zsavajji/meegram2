# MeeGram `feature/optimization` — validation plan

Branch: `feature/optimization`, based on `bc03881` (0.3.8). Ten performance changes
across 20 files. Nothing in them has been compiled or run: they were written on macOS,
which has no Qt 4. This plan is what turns them from "written" into "measured", and it
is meant to be executed by an agent with the Linux cross-build box and SSH to the N9.

This file is a hand-off, committed on the branch so it travels with the changes it
validates. It is not part of `docs/` and is not meant to outlive the branch: once the
numbers are in `docs/profiling.md` and `docs/notification-startup.md`, delete it.

Read `CLAUDE.md` at the repo root first, then `docs/profiling.md` and
`docs/notification-startup.md` for the measurement discipline this plan reuses. The rules
that matter most while running it:

- `killall meegram meegramd`, never busybox `pkill -f` (it matches nothing and exits 0;
  stacked instances share one daemon and produce an empty chat list that reads like a bug).
- Wait for the 1-minute load average under ~0.5 before every gesture window. Killing the
  app makes applauncherd prestart other apps and skews every number ~10×.
- The device is on the charger for every run. The N9 scales CPU with battery state.
- Run-to-run variance is ~16%. One run is not a result. Three runs, report the median.
- Restart `meegramd` together with the UI unless the scenario says "warm daemon".
- Markers and PROF tables land in `~/.meegram/meegram.log` on device
  (`-DMEEGRAM_FILE_LOG=ON`, the default). `invoker` discards stderr, so the file is the
  only place they exist when launched from the launcher. Launched over SSH they also print.
- `scp` of a running binary fails with "Text file busy" — killall first, do not
  `2>/dev/null` it.
- Installing needs real root; `dpkg -i` over plain ssh does not work. Hand the `.deb` to
  the maintainer for the install step, or use the established `devel-su` path.

## 0. What changed, and the rollback map

Each row is independently revertible with `git checkout bc03881 -- <files>`, except
where noted.

| # | Change | Files | Revert note |
|---|---|---|---|
| 1 | `ChatPage.qml` compiled once at idle, kept on `appWindow.chatPageComponent` | `resources/qml/main.qml`, `resources/qml/MainPage.qml` | — |
| 2 | One queued `Client::dispatch` per update; `result()` emitted on the GUI thread; object freed after the last slot | `src/Client.hpp`, `src/Client.cpp`, `src/ClientProxy.cpp`, comment-only in `src/MessageModel.{hpp,cpp}`, `docs/architecture.md`, `CLAUDE.md` | Shares `ClientProxy.cpp` with #3. To revert #2 alone, restore `Client.hpp`, `Client.cpp`, and in `ClientProxy.cpp` only the `dispatch`/tail-of-`handleLine` hunk and the header comment. |
| 3 | Reader thread newline scan keeps its offset (`scanned`/`consumed`) | `src/ClientProxy.cpp` (the `initialize()` loop) | Shares the file with #2. |
| 4 | Daemon send cursor (`Connection::sent`, `pendingBytes`) instead of `erase(0, n)` per write | `src/daemon/main.cpp` | — |
| 5 | `Chat` and `File` no longer keep their `td_api` shells | `src/Chat.{hpp,cpp}`, `src/File.{hpp,cpp}` | — |
| 6 | Emoji table without prebuilt `<img>` tags | `src/Utils.cpp` (`EmojiEntry`, `emojiTable`, `replaceEmojiSized`) | Shares the file with #7. |
| 7 | `Utils::emojiCategory` cached per category | `src/Utils.cpp` | Shares the file with #6. |
| 8 | Lottie reuses one `QImage` frame buffer, draws with `drawImage` | `src/LottieAnimation.{hpp,cpp}` | — |
| 9 | Photo viewer caps the full-size decode (`sourceSize.width`) and drops the preview once covered | `resources/qml/PhotoViewPage.qml` | — |
| 10 | Two per-row QML warnings removed | `resources/qml/components/ListItem.qml`, `resources/qml/components/DrillDownDelegate.qml` | — |

Two arms throughout: **A** = `bc03881` (main), **B** = the branch. Build both into
separate trees so switching is a `scp`, not a rebuild: `build-app` for B and, say,
`build-base` for A. Package both. Keep the `.deb` files named by arm.

## 1. Build gates (no device needed)

All must pass before anything is installed.

```sh
# B, the shipping configuration. Release is the default; -Werror is Debug-only, so also
# do one Debug configure to get the warnings as errors on the changed files.
cmake -B build-app -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake -DQT_SDK_PATH="$QT_SDK_PATH" \
      -DBUILD_HARMATTAN=ON -DMEEGRAM_JSON_TRANSPORT=ON -DMEEGRAM_PROFILE=ON
cmake --build build-app -j4

cmake -B build-debug -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake -DQT_SDK_PATH="$QT_SDK_PATH" \
      -DBUILD_HARMATTAN=ON -DMEEGRAM_JSON_TRANSPORT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j4

# The in-process transport: src/Client.cpp changed too and nothing else compiles it.
cmake -B build-inproc -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake -DQT_SDK_PATH="$QT_SDK_PATH" \
      -DBUILD_HARMATTAN=ON -DMEEGRAM_JSON_TRANSPORT=OFF
cmake --build build-inproc -j4

# Self-checks. json_roundtrip and notifier_check are ARM binaries; run them on device.
cmake --build build-app --target json_roundtrip notifier_check
```

Gate: all four builds clean, Debug with zero warnings in the changed files. Record the
warnings from any file in the rollback map verbatim.

On device, after install: `/opt/meegram/bin/json_roundtrip` and
`/opt/meegram/bin/notifier_check` (copy them over by `scp`; they are not packaged). Both
print OK. `notifier_check` prints two lines about a missing
`com.meego.core.MNotificationManager` when run off-device only; on device it must not.

Two changes have host-side checks that need no device at all:

- #3: the scan loop is pure C++. Extract it into a 30-line harness that feeds a 3 MB
  single-line string in 8 KB pieces plus a mix of short lines and empty lines, and
  asserts every line comes out once, intact, in order, with empty lines skipped. This
  is the one change whose logic is fully testable on the host, so do it.
- #4: same for `flushOutgoing` with a mock `send` that accepts N bytes then returns
  EAGAIN: assert the cursor advances, the compaction runs when `sent > size/2`, the
  drained queue clears, and `pendingBytes` matches what was not accepted.

## 2. Functional regression pass

Every item is a behaviour that a change could have broken. Run on arm B with the
shipping configuration first; the in-process build gets one abbreviated pass at the end.
Record pass/fail and the log lines for any fail.

**Startup, transport, dispatch (#2, #3, #4)**

1. Fresh sign-in from a wiped state (`rm -rf ~/.meegram ~/.config/insider`; kill both
   processes; start): phone number → code → (password) → chat list. Every auth state is an
   update that now goes through `dispatch`. Then sign out from Settings and sign in again.
2. Warm-daemon restart: kill only `meegram`, relaunch. Chat list populates, folder tabs
   present, sender names present, connection state in the header. This is the
   `getCurrentState` replay path.
3. Notification tap, warm daemon (recipe in `docs/notification-startup.md`, "Reproducing"),
   for a supergroup **outside** the first synced batch. The tapped chat opens, on the
   newest message. Repeat with the app already running and a chat page already open: the
   stack pops to the list and opens the tapped chat, no duplicate pages.
4. Saved Messages from the menu on a warm daemon that has never announced it (fresh
   daemon, then `Saved Messages` immediately). Opens, via `fetchChat` → `injectUpdate`.
5. `killall meegramd` while the app is on the chat list and again while a chat is open.
   The app reconnects within ~2–10 s (`meegramd went away; reconnecting` in the log) and
   keeps working; sending a message afterwards succeeds.
6. Language pack: `rm ~/.meegram/tdlib/langpack.cache` and set
   `languagePackFetchedAt=0` in `~/.config/insider/MeeGram.ini`, then launch. The 1.8 MB
   pack arrives on one line through the new scan; the UI is translated after the refresh
   (open Settings: rows read "Language", not a key). Then `grep -c undecodable
   ~/.meegram/meegram.log` **must be 0** — any splitting bug shows up as an undecodable
   line, which is the strongest correctness check #3 has.
7. Replay fidelity (#4): with the daemon started by hand with `--trust-any-peer`, use
   `tools/daemon_probe.c` (build: `arm-none-linux-gnueabi-gcc -static -O2 -o daemon_probe
   tools/daemon_probe.c`) to send `getCurrentState` with an `@extra` and count the lines
   that come back until the `{"@type":"ok","@extra":...}` acknowledgement. Compare the
   type histogram (`grep -o '"@type":"[a-zA-Z]*"' | sort | uniq -c`) against arm A on the
   same account within a minute. Counts must match (a handful off by one is a chat that
   left the daemon's memory between runs; anything else is a relay bug).
8. Backpressure: still with the probe, request `getCurrentState` and do **not** read for
   60 s. The daemon log must show `dropping a client N bytes behind` with N > 8 MiB only
   if the replay exceeds it (it will not on a 5.5 MB account); the real UI must remain
   responsive throughout, and reconnect cleanly afterwards.

**Value objects (#5)**

9. Avatars: empty `~/.meegram/tdlib/profile_photos`, relaunch, scroll the list. Avatars
   fill in as downloads complete (the `File` in `StorageManager`'s map is what changes).
10. A photo message downloads on sight and swaps from scrim to image; a document shows its
    size before download and opens after; the profile page's big photo loads.
11. Pin, mute, mark unread, delete a chat from the list menu; the row reflects each.

**Emoji (#6, #7)**

12. A message with mixed text and emoji; a message of one, two and three emoji (32/24/16 px
    big-emoji sizes); a chat whose title contains emoji, long enough to elide; reaction
    pills with icons; the reaction picker grid.
13. Emoji picker: open it, visit all seven tabs twice. Pick one from each tab; it lands in
    the composer.

**Lottie (#8)**

14. Sign-in code page and password page animations play (fresh sign-in from item 1).
15. An animated (tgs) sticker plays once on arrival and once when scrolled back into view
    after the delegate was rebuilt. A webp sticker and a webm sticker (falls back to the
    emoji) are unaffected.

**Photo viewer (#9)**

16. Open a photo whose original is wider than 1920 px (send one from a modern phone; check
    with `identify` or the file size). Preview shows immediately, spinner, full image
    replaces it, pinch to 4× pans the whole image, Save writes the original to
    `~/MyDocs/meegram/Pictures`. Then a small photo (< 480 px): unchanged.

**Warm-up (#1)**

17. From a cold start, do nothing for 10 s after the chat list appears, then open a chat.
    Opens normally. Then, from another cold start, open a chat **within 2 s** of the list
    appearing (before the warm-up fires). Opens normally.
18. Open a chat, back, open another, back, open the first again. No leaked pages, no
    "Error loading component" in the log.

**Warnings (#10)**

19. Open New Group, tick two contacts; open Settings. Then
    `grep -c "Unable to assign\|ReferenceError" ~/.meegram/meegram.log` must be **0**
    since launch (it was ≥1 per ticked row and per settings row before).

**In-process transport build** (`build-inproc`, once)

20. Install it, sign in, open a chat, send a message, receive one, tap a notification.
    This covers `Client.cpp`'s copy of `dispatch`. Reinstall arm B afterwards.

## 3. Measurements

All with `-DMEEGRAM_PROFILE=ON` on **both** arms. Same account, same device, charger,
load average < 0.5 before each window. Three runs each, medians into the table in §4.
Restart both processes between runs unless the scenario says "warm daemon".

Launch template for anything that needs the log on a terminal:

```sh
p=$(pidof meegotouchhome)
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$p/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p" | head -1)
export DISPLAY=:0
killall meegram meegramd; sleep 3; killall -9 meegram meegramd
rm -f ~/.meegram/sock ~/.meegram/meegram.log ~/.meegram/meegramd.log
cat /proc/loadavg                       # wait for < 0.5
/opt/meegram/bin/meegram &
```

Markers: `grep -a "MEEGRAM MARK" ~/.meegram/meegram.log`. PROF tables: `grep -a -A40
"MEEGRAM PROF" ~/.meegram/meegram.log | tail -45` for the last dump; deltas between two
dumps bracket a gesture (totals are cumulative, never add rows together).

Process CPU and memory: `awk '{print $14+$15}' /proc/$(pidof meegram)/stat` is total
CPU in jiffies (100/s); `grep -E "VmRSS|VmHWM" /proc/$(pidof meegram)/status` is current
and peak resident set. Per-thread: `/proc/<pid>/task/<tid>/stat`, same fields. The GUI
thread is `tid == pid`; the socket reader is the **lowest tid above it** (it is created in
`Client`'s constructor, before any image thread exists).

### M1 — first chat open after idle (#1)

Warm daemon. Launch, wait for `chat-layout-loaded`, wait a further **10 s** doing
nothing, open a mid-list chat.

| marker delta | A expected | B target |
|---|---:|---:|
| `chat-selected → chatpage-compiled` | ~980 ms | < 60 ms |
| `chatpage-compiled → chatpage-completed` | ~540 ms | unchanged |
| `chat-open-begin → chatpage-pushed` | ~1.6 s | ~0.6 s |

Also record: the GUI-thread CPU spent between `chat-layout-loaded` and the tap (B should
show ~1 s more, which is the compile moved to idle — that is the cost side, and it must
not show up as a visible stall while the user is scrolling; note whether it did).

Pass: B's compile delta is under 60 ms on all three runs and nothing else in the sequence
got slower.

### M2 — notification tap, warm daemon (#1, #2)

The recipe in `docs/notification-startup.md`. Two variants:

- **Cold app** (tap starts the process): `notification-tap → chatpage-pushed`. Expected
  ~4.2 s on A. On B the warm-up has not fired yet at that point, so the target is **no
  regression**; any gain here comes from #2.
- **Running app**, idle ≥10 s on the chat list: tap. Target: the `chatpage-compiled`
  delta near zero, tap-to-page under 1 s.

### M3 — startup replay cost (#2, #3, #4)

Warm daemon. Launch and sample at `t = 30 s` after `main-entry`:

| measure | how | A expected | B target |
|---|---|---:|---:|
| GUI-thread CPU, 0–30 s | `task/<pid>/stat` utime+stime | baseline | lower; the 4→1 events per update across ~2200 updates is the mechanism, expect 0.2–0.5 s |
| reader-thread CPU, 0–30 s | lowest tid above pid | ~1.7 s (measured on A after the split) | unchanged unless the pack refreshed |
| daemon CPU across the same window | `/proc/$(pidof meegramd)/stat` | baseline | lower; #4 removes the per-write memmove |
| `getCurrentState` delivery, daemon → client | probe, first line to `ok` | ~1.6 s | ≤ A |
| `undecodable` lines in the app log | grep | 0 | **0** |

Then the pack-refresh variant for #3: force the refresh (item 6 in §2), relaunch, and
measure reader-thread CPU over the first 30 s. On A the 1.8 MB line costs a quadratic
rescan; on B it should cost the decode alone. Report both numbers; a difference under
the 16% noise is fine — this change is about not being quadratic on bigger lines, and
correctness (the grep) is the gate.

### M4 — steady-state and drift RSS (#5, #6)

Warm daemon. Launch, let the list settle, read `VmRSS` at `t+2 min` on the chat list
without touching it. Then scroll the whole list to the bottom and back, open a chat with
emoji in it, read `VmRSS` again. Then leave it 10 minutes idle and read once more.

| measure | A | B target |
|---|---:|---:|
| RSS, chat list settled | ~78 MB | 1–1.5 MB lower (shells + emoji tags) |
| RSS after a full scroll + one chat | baseline | ≤ A |
| RSS drift over 10 idle minutes | baseline | equal to A (this is the leak check for #2: `dispatch` frees every update) |

Also `VmHWM` at the end of the run, both arms.

### M5 — sticker playback CPU (#8)

Open a chat with an animated sticker at the bottom so it plays on arrival. Read process
CPU immediately before the sticker appears and 5 s later. Three stickers, three runs.

| measure | A | B target |
|---|---:|---:|
| CPU jiffies over one 3 s playback | baseline | lower; the allocation and pixmap conversion per frame is gone |
| `frame` scope calls during playback | baseline | equal (no change in repaint count) |

### M6 — photo viewer peak (#9)

Open the large photo from §2 item 16, wait for the full image, then read `VmHWM`. Repeat
on A. Also note the decode time from tap to the full image appearing (stopwatch or the
BusyIndicator disappearing; no marker exists for it).

| measure | A | B target |
|---|---:|---:|
| `VmHWM` minus RSS before the tap | ~ +26 MB for a 2560 px photo | ≤ +15 MB |
| pixels reachable at 4× zoom | all | all up to 1920 px wide; state what was lost past that if the photo was wider |

### M7 — emoji picker tab switch (#7)

With PROF on, open the picker and visit each tab three times. In the last PROF dump,
`Utils::emojiCategory` `calls` must equal the number of **distinct categories visited**
(≤ 9) on B, and one per visit (≤ 27) on A. Record both totals.

### M8 — dispatch fan-out sanity (#2)

Not a timing, a count. In the last PROF dump after M3, `StorageManager::handleResult` is
not instrumented; instead confirm through behaviour: chat count on the list equals A's,
folder tabs equal, `updateFile` still reaches bound files (item 9). If a subscriber were
missed, chats or avatars would be missing, not slow.

## 4. Reporting

One table, medians of three, A and B side by side, with the per-run values in an
appendix. Every "target" above is a hypothesis; write the number that came out, not the
one that was hoped for. If a target was not met, say by how much and whether the
functional pass for that change still held — a change that is neutral and correct stays;
a change that regresses anything reverts by the map in §0.

Where the results go: `docs/profiling.md` for M1, M3–M8 (a new dated section, same
format as the existing ones), `docs/notification-startup.md` "After" table for M2. That
is the repo's rule — measurements and remediations are committed, plans are not — which
is why this file is a temporary exception and should be deleted once the numbers are in.
The MainPage comment for #1 already names the variant that was measured; add the numbers
next to it in the doc, not in the comment.

Things to write down even if they look boring:

- Load average at the start of each window.
- Battery state and charger.
- Which arm was installed (`dpkg -l meegram` shows 0.3.8 for both; use the `.deb` name).
- The daemon that was running (restarted, or warm from the previous run).
- Any line in either log matching `rejecting|undecodable|Unable to assign|ReferenceError|
  failed`, with the scenario it came from.

## 5. If something fails

- **Build error in a changed file**: fix the syntax if it is obviously a typo (these were
  written without a compiler); anything else, revert that change by the map and note it.
- **Crash on startup or on first update**: suspect #2 first. Revert `Client.hpp`,
  `Client.cpp` and the `dispatch` hunks in `ClientProxy.cpp`, rebuild, retest. The old
  path had one queued call per subscriber; the new one runs them in one event. A crash
  would mean a subscriber depended on that gap.
- **Chat list empty or partial after a warm start**: #4 or #3. Run §2 items 6 and 7; the
  `undecodable` grep and the probe histogram tell them apart (a relay fault drops or
  splits lines; a scan fault produces undecodable ones).
- **Avatars or file states stop updating**: #5. `File::setFile` now returns early on a
  null object where it used to fall through; confirm `updateFile` lines in the daemon log
  still arrive for the file id, then revert #5.
- **Emoji missing or wrong image**: #6. `replaceEmojiSized` now formats the tag from
  `entry->emoji->filename()` on every hit; a wrong filename here would be a wrong table
  pointer. Revert #6 and #7 together (same file).
- **Sticker draws a stale or blank frame**: #8. Confirm on both the GL and the software
  viewport (`-DMEEGRAM_GL_VIEWPORT=OFF` build) — the frame buffer reuse relies on
  `QImage::bits()` bumping the cache key, which is what invalidates a cached texture.
- **Photo viewer soft at high zoom**: #9 by design above 1920 px; if it is soft below
  that, `sourceSize.width` was not the value intended — print `flick.width`,
  `flick.height` and the computed cap with `console.debug`.
