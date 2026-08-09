# Handoff: the notification cold start

Opening the app by tapping a notification, against the resident daemon that posted the
banner, took **27 s to reach the message list**. 20 s of that was one wait, and the cause
was not what the first version of this document guessed. It is now measured, and fixed.

Measured on device 2026-08-09, Nokia N9 (RM696), real account, warm 119 MB TDLib database,
`-DMEEGRAM_PROFILE=ON`, `MEEGRAM_GL_VIEWPORT=OFF`.

## Measure the warm daemon, not the cold one

The repro at the bottom of this document kills `meegramd` along with the UI. That is not
the notification path: the daemon posted the banner, so by definition it is running and
synced when the tap arrives. The two runs are not variations of one number, they are
different problems, and the real one is the worse of the two:

| | cold daemon | **warm daemon (the real path)** |
|---|---:|---:|
| TDLib authorization wait | 1669 ms | **13 ms** |
| gap before the chat opens | 5289 ms | **19863 ms** |
| total to the message list | 12570 ms | **27070 ms** |

Killing the daemon hides the bug by making the state it dumps small.

## The measurement

Markers come from `MEEGRAM_MARK` (`src/ScopeTimer.hpp`) and land in
`~/.meegram/meegram.log`. `t=` is milliseconds since `main()`; `+` is since the previous
marker.

```
t=  320ms  pre-setsource
t= 1758ms  +1438ms  busy-shown           QML scene compiled, spinner up
t= 1833ms  shown
t= 1836ms  notification-tap             D-Bus tap delivered
t= 1852ms  chat-open-deferred           chatManager null - TDLib not authorized yet
t= 3521ms  + 939ms  chat-open-flushed   authorizationStateReady, tap released
t= 3522ms  chat-open-begin              openChat() -> false, chat not in StorageManager
t= 3580ms  app-initialized
t= 4061ms  chat-layout-loaded           chat list populated
t= 9352ms  +5289ms  chat-open-begin     <-- the problem: getChat finally answered
t= 9360ms  chat-selected
t=10340ms  + 980ms  chatpage-compiled
t=10882ms  + 542ms  chatpage-completed
t=10978ms  chatpage-pushed
t=12570ms  +1591ms  messages-shown      first getChatHistory
```

The warm-daemon run, which is the one that matters:

```
t=  350ms  pre-setsource
t= 1819ms  +1468ms  busy-shown           QML scene compiled, spinner up
t= 1890ms  notification-tap
t= 1894ms  chat-open-deferred            chatManager null - not authorized yet
t= 1907ms  + 13ms   chat-open-flushed    authorizationStateReady, tap released
t= 1908ms  chat-open-begin               openChat() -> false, chat not in StorageManager
t= 4007ms  chat-layout-loaded
t=24317ms  +19863ms chat-open-begin      <-- the problem
t=24321ms  chat-selected
t=25296ms  chatpage-pushed
t=27070ms  +1773ms  messages-shown
```

## Why it waited — measured, not guessed

Three observations, none of which needed a build.

**1. The reader thread is the whole gap.** Sampling `/proc/<pid>/task/*/stat` through a run:
the UI's socket reader (`Client::initialize`'s `jthread`) burns 11.8 s of CPU across the
20 s window and stops at the exact moment the chat opens. The GUI thread is at 22%, the
daemon's TDLib thread is idle. The daemon produced everything promptly; the UI could not
read it.

**2. `getChat` is not slow. It is instant.** A fake UI attached to the socket
(`--trust-any-peer` plus a dozen lines of perl) and asked the warm daemon directly:

| request | response | time |
|---|---:|---:|
| `getChat` on the tapped chat | 14 KB | **0.01 s** |
| `getLanguagePackStrings` | 1.8 MB | 4.1 s |
| `getCurrentState` | **5.5 MB** | 5.8 s |

`getCurrentState` answers on **one line**, and `Client::handleLine` decodes a line before
it looks at the next. The 14 KB answer the user is waiting for sits behind 7.3 MB of bulk
in a strict FIFO. That is the 20 s.

Inside the 5.5 MB: 543 `updateNewChat` (2.3 MB), 524 `updateChatLastMessage` (1.2 MB),
343 `updateUser` (780 KB), and 930 KB of `updateUserFullInfo` / `updateSupergroupFullInfo`
that nothing on screen reads. The whole of the daemon's memory, every launch, to show one
chat.

**3. Tap-to-open was resting on that dump.** `ChatManager::fetchChat` sent `getChat` and
waited for TDLib to push `updateNewChat`, because `StorageManager` learns chats from
nothing else. But `updateNewChat` is emitted **once per TDLib process**
(`send_update_new_chat` latches `d->is_update_new_chat_sent`) and the daemon's TDLib
outlives the UI. So against a warm daemon the reply arrived and the chat still was not in
the store; the retry missed again; `fetchChat`'s one-attempt latch stopped it there. The
chat only ever opened because `getCurrentState`'s replay happened to carry
`updateNewChat` for all 543 chats. Not slow — load-bearing luck.

## After

Same device, same account, same repro, 2026-08-09 17:26–17:35.

| | before | after |
|---|---:|---:|
| **warm daemon**, tap → chat page | 25296 ms | **4349 / 4226 ms** |
| warm daemon, gap waiting for the chat | 19863 ms | **121 / 442 ms** |
| warm daemon, UI reader-thread CPU | 11.86 s | **1.69 s** |
| warm daemon, peak RSS at chatpage-pushed | 75.7 MB | **55.0 MB** |
| cold daemon, gap waiting for the chat | 5289 ms | **200 ms** |
| `getCurrentState` delivery, daemon → client | 5.80 s | **1.63 s** |

The reader thread dropped 7× rather than merely being reordered, which was not the
expectation. Decoding one 5.5 MB `updates` object builds its whole tree at once; 2219
small lines never hold more than one update, and the allocator behaves completely
differently — which is the same reason peak RSS fell 20 MB.

Delivery got faster too: many small writes drain as the client reads them, where the
single blob stalled on POLLOUT against a socket buffer it could not fit in.

**Not fixed, and now the largest remaining item: the first `getChatHistory`.** It answered
in 864 ms on one run and 9765 ms on the next, from an identical chat page at 4.2 s. It is a
TDLib/network operation downstream of everything here and it was always in this timeline —
the old measurements just had it hidden behind a 20 s wait. Time to the *chat page* is now
stable at ~4.2 s; time to *messages* is not, and that is the next thing to measure.

## Verifying the split is faithful

The fake UI counts what arrives, so the split can be checked against the monolith it
replaces rather than trusted:

```
before:  1 line,     5500069 bytes   (543 updateNewChat, 524 updateChatLastMessage, ...)
after:   2219 lines, 5486079 bytes   (542 updateNewChat, 523 updateChatLastMessage, ...)
```

Type histograms match item for item. The handful of counts down by exactly one —
`updateNewChat`, `updateSupergroup`, `updateSupergroupFullInfo`, `updateChatLastMessage` —
are one supergroup that was no longer in the daemon's memory between the two runs, not
dropped records: a broken scanner loses arbitrary elements, not precisely the four
belonging to one chat. `Client::handleLine` logs every line it cannot decode and logged
none.

## What was changed

- **`src/daemon/main.cpp`** — `broadcastSplit` relays a multi-update response as its
  constituent updates, one per line, and answers the request with a plain `ok`. A brace
  scanner over TDLib's own buffer, no parse and no copy, so the relay stays a relay.
  Neither process holds 5.5 MB or the object tree decoded from it at once.
- **`src/ChatManager.cpp`** — `fetchChat` injects the returned `chat` as an
  `updateNewChat` instead of waiting for one that is never coming. This is the
  independent fix for observation 3: opening a chat by id now works against a warm daemon
  on its own, in 10 ms, whatever else is on the socket.
- **`src/AppManager.cpp`** — `chatManagerChanged` is emitted before the state replay is
  asked for, so a held tap puts its `getChat` and first `getChatHistory` on the socket
  first; and a cached language pack is only re-pulled every two days
  (`LanguagePackMaxAgeSeconds`), which takes the other 1.8 MB off the launch entirely.

## Already tried — do not redo

**Racing the sync against `getChat`.** `ChatManager` was made to also listen to
`StorageManager::chatUpdated`, so whichever landed first would release the open.
Implemented, measured (5560 ms → 5289 ms), reverted.

The conclusion drawn at the time — "the chat does not arrive in the startup sync burst at
all" — was wrong, and the experiment could not have shown otherwise: the chat *was* in the
burst, but the burst was one 5.5 MB line, so every chat in it landed at the same instant as
the `getChat` reply that was queued behind it. There was nothing for either side to win by.
Now that the daemon splits that line, the same race would be a real one — which is
precisely why it is not needed: `fetchChat` no longer depends on the replay at all.

**QML deferral and preloading.** `ChatListView` was extracted out of `MainPage.qml` so it
compiled on demand, then preloaded during the TDLib wait. Both measured, both a wash or
negative, both reverted. Numbers in the git history around `c69c1f7`. Summary: deferring
moved 461 ms off first paint but put 354 ms back onto time-to-chat-list, because the Loader
only switches once `chatManager` exists — i.e. at the *end* of the wait, so the compile
cannot overlap it. Preloading fixed that tail (878 → 468 ms) but extended
`shown → app-initialized` from 1250 to 1753 ms, because QML1 has no asynchronous component
creation and the compile blocks the GUI thread, delaying the TDLib messages that drive
`app-initialized`.

**QML precompilation does not exist here.** Verified against the sysroot: Qt 4.7.4 has no
Qt Quick Compiler (5.8), no disk cache (5.11), not even
`QDeclarativeComponent::Asynchronous` (4.8). `QDeclarativeCompiledData` lives in the engine
and dies with it. The ~1.4 s scene compile can be moved or reduced but never precomputed.

## What is left

**The first `getChatHistory`** — see the "After" table. Between 0.9 s and 9.8 s from the
same chat page, and now the biggest single number in the timeline. Measure it before
designing anything, the same way this was: the fake UI can ask a warm daemon for
`getChatHistory` directly and time the reply, which separates "TDLib is slow to answer"
from "the answer is slow to arrive".

**The replay is still 5.5 MB.** It no longer blocks anything the user is watching and it
costs 1.69 s of reader thread rather than 11.86, but it grows with the account rather than
with the screen, so the account that is twice this size pays twice.

**The chat list should be demand-loaded, and `fetchChat` is now the primitive for it.**
`getChats(chatList, limit)` returns ids from the already-loaded list — small, and it does
not 404 the way `loadChats` does on a warm daemon. `getChat` on each row being rendered is
14 KB and 10 ms, and now lands in `StorageManager` by itself. A first screen is ~10 chats,
so ~140 KB against 5.5 MB, and flat as the account grows.

What has to be got right: `positions` (ordering) rides on the `chat` object and on
`updateChatLastMessage`, so `ChatModel` has to keep working from the first of those alone.
That is the piece to design before touching it.

Two things measured along the way that are *not* worth chasing:

- **Message history is already lazy.** Nothing in the 5.5 MB is chat history — TDLib
  carries only each chat's last message, and `getChatHistory` runs when a chat is opened
  (`MessageModel`'s constructor). The waste is breadth, not depth.
- **The `*FullInfo` updates** are 930 KB of the replay and nothing on screen reads them at
  startup, but they are TDLib's to include and the relay should not be filtering by type.
  Demand-loading the list removes them along with everything else.

## Reproducing

Device is `ssh user@n9`. A tap can be simulated exactly — `com.meegram` is D-Bus
activatable, so calling it with the app down reproduces the notification cold start end to
end.

**Kill only `meegram`.** Leaving `meegramd` up is the real path and the slow one; the
sequence below kills both and measures the easier problem. Keep it for comparing the two.

To attach a fake UI to the socket — which is how the response sizes above were taken, with
no build and no instrumentation — restart the daemon by hand with `--trust-any-peer`, run
the real UI once to give TDLib its parameters and let it sync, kill the UI, and then talk
to the socket directly. `perl` is on the device; `socat` and `nc` are not.

The full sequence for the cold-daemon comparison:

```sh
p=$(pidof meegotouchhome)
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$p/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p" | head -1)
export DISPLAY=:0

# killall, never pkill -f: busybox pkill matches nothing and exits 0, leaving stacked
# instances that share one meegramd and look like an empty chat list.
killall meegram meegramd; sleep 3; killall -9 meegram meegramd
rm -f ~/.meegram/sock ~/.meegram/meegram.log

# Wait for the load average under ~0.5 first. Killing the app frees ~70 MiB and
# applauncherd answers by prestarting camera-ui, fenix and call-history unprompted; a run
# measured in that window comes out ~10x slow across every site.
cat /proc/loadavg

dbus-send --session --print-reply --dest=com.meegram \
  /notification com.meegram.Notification.openChat string:<chatId>

sleep 45
grep -a "MEEGRAM MARK" ~/.meegram/meegram.log
```

`-1001101425425` is a supergroup on the test account that reproduces the miss. Any chat
outside the first synced batch should do.

Other tools already in the tree:

- `MEEGRAM_QML_BENCH=2` — compiles every `.qml` on a fresh engine, two passes, and exits.
  Per-file table for the scene compile.
- `MEEGRAM_HEADLESS=1` — syncs TDLib without building the QML scene.
- `MEEGRAM_KEEPALIVE=1` — keeps the process alive on window close and drops the scene.

## Constraints

- **Do not build or commit.** The maintainer does both. Verify C++ with a standalone `g++`
  compile of the logic where possible.
- **Qt 4.7.4 / QML1.** Never expose a `qlonglong` to QML — it is boxed as a double and the
  way back corrupts. Chat ids cross as decimal strings; this is why every `Q_INVOKABLE` on
  the chat path takes a `QString`.
- **TDLib callbacks run on the reader thread.** `Client::send` callbacks are not on the GUI
  thread; hop with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Every crash in
  this codebase so far has been object lifetime or a cross-thread model mutation.
- **`result()` is emitted queued** and disposal is queued behind it; four subscribers hold
  the raw pointer and none may delete it. See the header comment in `src/ClientProxy.cpp`.
- **Daemon TDLib log verbosity is now 1** (`src/daemon/main.cpp:587`). It was unset, which
  meant TDLib's default — 10.9 MB written to eMMC during a cold start at ~1.5 MB/s, plus log
  rotation firing mid-startup. If you raise it for debugging, put it back; leaving it up
  costs seconds of every cold start and will silently poison any measurement.
- **`invoker` swallows stderr**, so launch over SSH when you want the log. `openLog`
  redirects stderr into `~/.meegram/meegram.log` from inside `main()`, so markers land there
  either way.

## State of the tree at handoff

Uncommitted, on top of `c69c1f7`:

- `src/daemon/main.cpp` — TDLib log verbosity 1
- `src/AppManager.{hpp,cpp}` — notification tap latched until the QML scene exists; the tap
  was previously emitted into a scene with no `Connections` and dropped entirely on every
  cold start. Also `signedOut`, seeded from `Settings::wasAuthorized`
- `src/Settings.{hpp,cpp}` — persisted `wasAuthorized`
- `src/ChatManager.cpp` — `handleChatFetched` ignores replies for a chat that is no longer
  the one being waited on, so a late reply cannot push a page the user has navigated away
  from
- `resources/qml/MainPage.qml` — Loader keyed on `signedOut` rather than `!chatManager`;
  per-component startup markers; `AboutDialog` built on demand
- `src/main.cpp`, `src/Utils.*`, `src/ClientProxy.cpp`, `src/ScopeTimer.hpp`, QML call sites
  — the marker timeline and `MEEGRAM_QML_BENCH`
