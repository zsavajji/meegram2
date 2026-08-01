# Profiling: what the scroll work actually bought

Measured on device, 2026-07-31. `docs/features.md` said the performance work was
"implemented; none of it has been measured" — this is the measurement. It settles three
of the five changes on the scroll path, corrects the reasoning behind a fourth, and finds
that the thing dominating chat-list scrolling is none of them.

**One arm, one run per scenario.** The plan called for a baseline arm and a median of
three runs; neither happened. Every number here is arm B — current `main` — and every
verdict below rests on **ratios within a single run**, which is what the counters were
designed for: instrument the branch and the total, and the `calls` ratio is the answer
without a baseline binary. The one change that genuinely needs a second binary, #2, is
therefore the one change with no verdict.

## Run conditions

| | |
|---|---|
| Device | Nokia N9 (RM696), `2.6.32.54-dfl61-20121301`, on wifi |
| Build | `-DBUILD_HARMATTAN=ON -DMEEGRAM_PROFILE=ON -DMEEGRAM_JSON_TRANSPORT=ON -DMEEGRAM_GL_VIEWPORT=ON`, `Release` |
| Account | Real account, warm database (`~/.meegram/tdlib`, 13 MB SQLite) |
| Launch | Over SSH — `invoker` in the `.desktop` swallows stderr (`docs/building.md:296`) |
| Charger | **Not verified.** See the caveat below |

The N9 scales its CPU with battery state, and the plan called being on the charger
non-negotiable. It was never confirmed during the session, so absolute times here — the
82 ms avatar decode especially — may not be reproducible run to run. **The ratios are
unaffected**, since both sides of every ratio come from the same run under the same
governor, and the ratios are what the verdicts rest on.

### Two device quirks that invalidate runs silently

Both cost a run before being spotted. Neither is obvious from the symptom.

**`killall`, never `pkill -f`.** Busybox `pkill -f` matches nothing on this device and
still exits 0, so a launch script that kills and relaunches quietly leaves the old process
running. Three instances stacked up; they share one `meegramd` over `~/.meegram/sock`,
and the newest UI showed an **empty chat list** while its answers went to a sibling. It
reads exactly like a TDLib bug.

**Wait for the load average before gesturing.** Killing the app frees ~70 MiB, and
Harmattan's applauncherd answers by prestarting other applications — `camera-ui`,
`fenix`, `call-history` were all spawned unprompted right after a relaunch, taking the
load average to **4.05** on a single core. One S1 run was measured in that window and
came out ~10× slow across every site (`ChatPhotoProvider::requestImage` at 1.1 s a call
against 82 ms), which is enough to invent or erase any effect being measured. Before each
gesture window:

```sh
ssh user@n9 'cat /proc/loadavg'   # wait until the 1-minute figure is under ~0.5
```

Note it is a decaying average, so it lags the burst it reports by about a minute.

**Restart `meegramd` with the app.** These runs were taken when a UI starting against a
*resident* daemon came up with an empty chat list: the daemon's TDLib had already emitted
its `updateNewChat` burst to the previous UI, and `loadChats` answered 404 rather than
re-sending. `AppManager::restoreState` has since fixed that, so a warm start is a valid run
now — but it is a *different* one, because the state arrives as a single `getCurrentState`
reply rather than spread across the sync. Keep restarting the daemon for anything compared
against these tables:

```sh
killall meegram meegramd; sleep 3; killall -9 meegram meegramd
# then launch with the session bus borrowed from the running desktop:
p=$(pidof meegotouchhome)
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$p/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p" | head -1)
export DISPLAY=:0
/opt/meegram/bin/meegram 2>&1 | tee /home/user/prof-<arm>-<scenario>-<run>.log
```

That the daemon surviving the UI left the next UI with an empty list was a **defect**, not
a profiling artefact — surviving the UI is the daemon's whole purpose. Fixed since, in
`AppManager::restoreState`.

### Reading the tables

Totals are cumulative since process start, so a scenario's number is the **delta between
two dumps** bracketing the gesture, never a dump's absolute value. Nested scopes both
record and the inner is included in the outer, so rows are never added together.

`frame` counts `QGraphicsView` paints (`main.cpp`, `ProfiledView`); fps is Δcalls / 5 s.

**Noise floor: ~5 µs**, not the 1–2 µs the plan assumed. `frame` wraps a call that does
nothing and reports 4.9–6.7 µs, so that is what the instrument costs itself. Any site
reporting under ~6 µs is at the floor and its avg means nothing beyond "free".

## S0 — startup markers

Four consecutive launches, identical to the KiB at `view+gl`:

| marker | this build | `docs/restructuring.md:43` | delta |
|---|---:|---:|---:|
| `qt-app` | 10,288 KiB | 10,524 KiB | −236 KiB |
| `view+gl` | 24,016 KiB | 15,284 KiB | **+8,732 KiB** |
| `tdlib-client` | 27,616–27,940 KiB | 15,524 KiB | +12,092 KiB |
| `qml-scene` | 46,316–46,664 KiB | 44,108 KiB | +2,208 KiB |

The reference run in `docs/restructuring.md` was built with **`MEEGRAM_GL_VIEWPORT=OFF`**
(`build-app`'s cache still says so); this one has it ON. The marker prints under both
settings whether or not a `QGLWidget` was ever created, so the doc's `view+gl` is
`QDeclarativeView` alone.

**The SGX GL context costs ≈ 9.1 MiB of resident set.** That is a real input to the
`MEEGRAM_GL_VIEWPORT` question at `CMakeLists.txt:209`, and it arrived without the A/B
run it was supposed to need. Whether it buys frames back is still unmeasured (S5 below).

`tdlib-client` is +3.6 MiB here against +0.2 MiB in the doc with the transport identical
in both, which is **not explained**. It is the one open thread in S0.

## S1 — chat-list scroll

The plan's S1 assumed "avatars already cached". With a long chat list that state cannot be
reached by scrolling forward: rows enter the viewport for the first time the whole way
down. So S1 split into two windows on one process.

### S1a — first pass down fresh rows (~60 s)

| site | Δ calls | Δ total | per call |
|---|---:|---:|---:|
| `frame` | 1229 | — | ~40 fps |
| `ChatModel::data` | 3283 | **43.8 ms** | 13 µs |
| `ChatModel::formattedRow.miss` | 97 | 20.7 ms | 213 µs |
| `ChatPhotoProvider::requestImage` | 92 | **7,571.9 ms** | **82 ms** |
| `Utils::replaceEmoji` | 469 | 155.6 ms | 332 µs |
| `ChatModel::sortChats` | 0 | — | — |
| `rss=` | +7.9 MiB | | |

97 cache misses against 92 avatar decodes is the signature of 97 rows displayed for the
first time — the two track 1:1 because they are the same event. (Eviction would look
different: decodes without misses, since the format cache would still hit.)

### S1b — re-scroll of the same range, no new territory (~60 s)

| site | Δ calls | Δ total | per call |
|---|---:|---:|---:|
| `frame` | ~500 | — | ~37 fps |
| `ChatModel::data` | 1632 | **9.6 ms** | **5.9 µs** — at the floor |
| `ChatModel::formattedRow.miss` | **0** | — | — |
| `ChatPhotoProvider::requestImage` | 30 | **1.4 ms** | **47 µs** |
| `Utils::replaceEmoji` | 233 | **88.5 ms** | 380 µs |
| `ChatModel::sortChats` | 0 | — | — |

Zero misses over 1632 reads: nothing is evicted, the cache holds. The same avatar costs
**82 ms on first display and 47 µs on re-display** — 1700×, so the provider path is fine
and the cost is decode-once.

**The model layer is not what makes this list scroll badly.** 3283 `data()` calls cost
43.8 ms across a full-length flick; avatar decoding in the same window cost **7.6
seconds**. The cached row is ~0.26% of what displaying a new row costs.

## S2 — chat-list scroll, cold avatars

`~/.meegram/tdlib/profile_photos` emptied (247 files, 2.8 MB), daemon and app restarted,
then a scroll down through fresh rows while avatars downloaded.

| site | calls | total | avg |
|---|---:|---:|---:|
| `File::setFile` | 339 | 56.7 ms | 167 µs |
| `File::fileChanged` | **339** | 42.4 ms | 125 µs |
| `ChatModel::data` | 442 | 17.7 ms | 40 µs |
| `ChatPhotoProvider::requestImage` | 25 | 76.0 ms | 3.0 ms |

**Ratio 1.00.** 61 avatars came back for 339 packets — 5.6 packets per file, every one of
which moved a property QML reads. The guard filters nothing here.

`requestImage` averages 3.0 ms rather than S1's 82 ms because during S2 the files had not
landed yet and the provider was returning early on missing files. S2's decode and fps
numbers are not comparable to S1's; its `File::` counters are the point.

## Photo-sized downloads (Twongo)

Same counters, a photo-heavy chat, files already partly uncached — no cache deletion
needed.

| | `setFile` | `fileChanged` | ratio |
|---|---:|---:|---:|
| S2, avatars (~12 KB) | 339 | 339 | **1.00** |
| Photos | 114 | 62 | **0.54** |

Same mechanism, opposite outcome, and the explanation is file size: a photo produces
intermediate progress packets in which nothing exposed moves, and those are suppressed. An
avatar is small enough that every packet it produces *is* a state transition.

`MessageModel::data` in the same run: 449 calls, 27.6 µs avg, 44 misses (**9.8%**), miss
167 µs — about 2× the chat model's warm cost per call, and clear of the noise floor.

## Second session: after remediation 1

`replaceEmoji` was moved out of the delegate bindings and into the cached row
(`TitleHtmlRole` / `SenderHtmlRole`; the plain roles stay plain, because they feed the
delete confirmation and the reply composer). Re-measured on a quiet device, same
gestures.

| | first pass (S1a) | warm re-scroll (S1b) |
|---|---:|---:|
| `ChatModel::data` | 406 calls, 29.2 ms | 5285 calls, 32.3 ms |
| — warm reads only | **6.7 µs** | **6.1 µs** |
| `formattedRow.miss` | 48, 558 µs each | **1** |
| `Utils::replaceEmoji` | 48 (one per miss) | **1** |
| `ChatPhotoProvider::requestImage` | 15, 10.8 ms each | **205, 83 ms each** |

**Remediation 1 works.** In the original S1b `replaceEmoji` ran **233 times for 88.5 ms**
with the format cache taking zero misses, because the call sat in a QML binding and re-ran
on every delegate rebind. It now runs **once**, across 5285 reads. The miss path absorbed
one emoji pass (213 µs → 558 µs) and the hot path is unchanged at ~6 µs. In the original
S1a the ratio was 469 emoji calls for 97 new rows — 4.8 per row; it is now exactly 1.

**And a bigger finding underneath it.** That warm re-scroll took **one** format miss and
**205 avatar decodes at 83 ms each — 17.1 seconds**, against 32.3 ms for every `data()`
call in the same window. 530×. The first session's S1b saw re-display cost 47 µs, but it
covered a short range whose avatars all fit in `QDeclarativePixmapStore`; over a longer
range the store thrashes, RSS climbs 7.1 MiB, and every row costs a full decode again.

So the "decode once, then it is free" reading from session one is wrong as a general
claim. It holds only while the working set fits, and on a real chat list it does not.
**Avatar decode is the chat-list scroll cost**, first pass and warm alike, and none of the
five changes touch it.

## S4 — message-list scroll (test chat: Dario, plain text)

Ran once the paging bug was fixed. Five pages of 20 landed, `count` 20 → 121, the view
restored onto the reading position each time.

| site | S4a — paging into history | S4b — warm re-scroll |
|---|---:|---:|
| `MessageModel::data` | 1586 calls, 56.7 ms | 2780 calls, 53.7 ms |
| — warm reads only | **10.9 µs** | **10.4 µs** |
| `MessageModel::formattedRow.miss` | 171, 241 µs each | 98, 262 µs each |
| `Utils::replaceEmoji` | 323, **105.6 ms** | 359, **182.2 ms** |
| `frame` | ~40 fps | ~30 fps |

**Change #1 is confirmed and the `m_formatted.clear()` suspicion is mostly cleared.** The
falsification bar was a miss/read ratio near 1.0, meaning constant invalidation; it came in
at **0.108**. Against 101 newly loaded rows there were 171 misses, so the per-page clear
(`MessageModel.cpp:785`) costs roughly 14 redundant re-formats per page — about 3.4 ms.
Real, and not worth restructuring for.

S4b's 98 misses are not invalidation either: no page landed in that window. They are rows
fetched during S4a and never displayed, because each prepend restores the view to the top
of the previous block — most of a fetched page is not seen until you scroll back over it.

**The message list's cost is emoji, not the model.** 359 `replaceEmoji` calls for
**182.2 ms** against 53.7 ms for every `data()` call in the same warm window — 3.4×, with
nothing new loaded. This is the same per-rebind pattern remediation 1 removed from the
chat list (`MessageDelegate.qml:39`, `:148`), but on message *content*, so the same fix
does not apply directly: a cached row would have to hold the rendered HTML per message,
keyed on the emoji size the delegate asks for.

## Third session: the two costs that actually mattered

Neither of these was on the plan's list. Both were found by the counters, both are one
small change, and together they are worth more than the five changes the plan set out to
measure.

**Avatar decode — `MaxCachedAvatars` 64 → 256** (`ChatPhotoProvider.cpp`). Comparable warm
re-scrolls, before and after:

| | 64 entries | 256 entries |
|---|---:|---:|
| `ChatModel::data` | 5285 calls, 32.3 ms | 4766 calls, 29.1 ms |
| — warm reads | 6.1 µs | 6.1 µs |
| `ChatPhotoProvider::requestImage` | **205** | **21** |
| `ChatPhotoProvider::decode` | — | **6** |
| decode time in window | **17,113 ms** | **39.2 ms** |
| `rss=` | 78.1 MiB | 77.7 MiB |

**17.1 seconds to 39 milliseconds**, for one constant and no extra resident set. The cost
lived in a cache size rather than an algorithm, which is why reading the code never found
it: the provider was already decoding at `sourceSize`, masking once and caching the
result. It just held 64 entries against a chat list far longer than that, so a warm
re-scroll missed on nearly every row. QML's own `QDeclarativePixmapStore` is ~2 MiB with
no public setter in Qt 4.7, so it will thrash regardless; what this cap decides is whether
a miss up there costs a shared `QImage` copy or a decode.

**Emoji substitution — a memo in `Utils::replaceEmojiSized`.** The delegates re-evaluate
these bindings on every rebind, and message *content* has no row cache to hold the result
in. Pure warm re-scroll, no paging:

| | before | after |
|---|---:|---:|
| `Utils::replaceEmoji` | 359 calls, **182.2 ms** | 323 calls, **3.0 ms** |
| per call | 507 µs | **9.3 µs** |
| `MessageModel::data` warm | 10.4 µs | 10.2 µs |

60× per call. The memo sits below the existing no-emoji fast path, which is an
allocation-free scan not worth hashing a string to skip, and covers every caller at once —
message text, captions, service messages, the chat header.

**What the decode split says.** `ChatPhotoProvider::decode` nested inside `requestImage`
reads 38.8 ms against 108.5 ms during a first-pass scroll. The 70 ms gap is far more than
the crop, the mask composite and the cache insert can cost on a 64×64 image; the likely
explanation is the pixmap reader thread being descheduled mid-function on a single core,
which the outer scope counts and the inner one partly escapes. If so, ~39 ms is the real
CPU cost of an avatar and the rest is contention — which argues for decoding *fewer*
avatars rather than decoding them faster, and the cache above is exactly that.

## Verdicts

| change | verdict | evidence |
|---|---|---|
| **#1** `MessageModel` row cache | **Confirmed.** Miss/read 0.108 against a falsification bar of ~1.0; warm reads 10.4–10.9 µs. The `m_formatted.clear()` suspicion costs ~14 re-formats a page, ~3.4 ms | S4a, S4b |
| **#2** RichText → PlainText | **Not measured.** The only change with no number. It needs arm A, which was never built — S4 now works, so this is one session away | — |
| **#3** preview newline strip | **Confirmed, and free.** Folded into `ChatModel::data`, which reads 5.9 µs warm — the floor | S1b |
| **remediation 1** emoji into the cached row | **Confirmed.** 233 calls → 1 across a warm re-scroll; hot path unchanged at 6.1 µs | second session |
| **#4** `File` emit guard | **Keep the code, fix the comment.** 46% of notifications suppressed on photos is a real win; 1.00 on avatars means it cannot help the case its own comment claims | S2 + Twongo |
| **#5** `sortChats` skip | **Better than the plan expected.** Swallowed 19 of 23 sorts during sync; the 4 that got through cost 75–296 ms *each*, inside the emit — that is the ListView relaying out | S1, go/no-go |

On **#5**: the plan predicted an idle account would prove nothing, and for steady-state
traffic that holds — zero sorts fired during either scroll window. But the startup sync
burst measured it by accident, and a suppressed `layoutChanged` is worth ~100 ms of view
relayout on this hardware. A near-zero reading on an idle account remains **not evidence
either way** for the busy-account case.

## What this found that the plan was not looking for

1. **Avatar decode was the chat-list scroll cost** — 17.1 s during a *warm* re-scroll that
   took one format miss, against 32.3 ms for the entire model layer. **Fixed** by raising
   the provider's cache from 64 to 256 entries: 39.2 ms, same scroll, no extra RSS. The
   remaining first-display cost is ~39 ms of decode per new avatar, and the next lever
   there is caching the masked 64×64 result on disk.
2. **`Utils::replaceEmoji` ran per display, outside the cache** — 233 calls and **88.5 ms**
   during a warm chat-list re-scroll where the C++ cache took zero misses, because it sat
   in a QML binding. **Fixed** for titles and sender names, re-measured at 1 call.
   For message content it was 182.2 ms during a warm message-list scroll, 3.4× the whole
   model layer — **fixed** with a memo inside `replaceEmojiSized`, now 3.0 ms. With it
   gone, `Utils::emojiOnlySize` is the larger of the two (312 calls, 10.7 ms, also
   per-rebind at `MessageDelegate.qml:37`), which is small enough to leave alone.
3. **The GL viewport costs 9.1 MiB** (S0).
4. **The instrument's own floor is ~5 µs on this hardware**, which sets the resolution
   limit for every future run.
5. **Device load moves everything by 10×**, and applauncherd creates it unprompted right
   after a relaunch. See the run conditions.

## Bugs found on the way

Measuring the message list meant fixing it first. Both of these were found by the
profiling session and are fixed in the tree.

  **The segfault** (`0b910d0`): the `getChatHistory` callback filled
  `m_messageMap`, inserted into `m_messages` and drove `begin/endInsertRows` on the TDLib
  worker thread while the GUI thread read all three in `data()`. Scrolling triggers the
  fetch, so it died mid-flick. The body now hops to the GUI thread. One session of
  scrolling without a crash; not proof, but the race is gone by construction.

  **Paging back into history was broken by that same fix**, and three `console.log` lines
  found in one run what two rounds of reading the code did not. Every call showed
  `loading=true` — the *page's* flag, not the model's — and `goToInitialPosition` never
  ran at all. One stuck flag, both symptoms: the chat opened on the oldest loaded message
  and `fetchOlder` was rejected on its first guard forever.

  `listView.onCountChanged` cleared that flag only when `messageModel.loading` was already
  false. Moving `handleHistoryResponse` onto the GUI thread made `endInsertRows()` fire
  the count change **synchronously, before `cleanupFlags()` clears the model's flag**, so
  the handler saw `loading` still true and skipped. Previously the model emitted across
  threads and the ordering came out the other way round. The handshake now runs from
  whichever of the two signals lands second (`settleIfLoaded`, driven by both
  `onCountChanged` and the model's `onLoadingChanged`), so it does not depend on the order.

  Two earlier attempts aimed at the wrong guard entirely — `followLast` and the
  edge-triggered `onAtYBeginningChanged` — and one of them shipped a regression that made
  chats open at the oldest message. The guard rework was kept because the deadlock it
  describes is real, but it was never what blocked paging. The lesson is cheap to state:
  three `console.log` lines answered in one run what two rounds of reading the code got
  wrong.

  **Found here, fixed since, and not a profiling artefact:** a UI restarting against a
  *resident* `meegramd` came up with an empty chat list, because the daemon's TDLib had
  already sent its `updateNewChat` burst to the previous UI and `loadChats` answers 404.
  Outliving the UI is the daemon's entire purpose, so this reached real users. The same
  hole swallowed `updateUser`, `updateChatFolders` and `updateOption`, so a warm start also
  had no sender names, no folder tabs and no `my_id`. `AppManager::restoreState` now asks
  `getCurrentState` on attach and replays the answer as updates.

## What is not measured

- **Arm A (baseline `249b0b0`)** — never built. Not needed for #1, #3, #4, #5, whose
  verdicts are within-run ratios; needed for #2, which is why #2 has no number.
- **S5, `MEEGRAM_GL_VIEWPORT=OFF`** — never built. The 9.1 MiB cost is known, the frame
  benefit is not.
- **Medians.** One run per scenario, not three.
- **Busy-account traffic**, out of scope by the plan's own decision.

## Reproducing

Counters are compiled out without `-DMEEGRAM_PROFILE=ON`, so the call sites stay in the
tree. `src/ScopeTimer.hpp` documents the mechanism; the sites that matter for a rerun:

| site | what its `calls` means |
|---|---|
| `frame` (`main.cpp`, `ProfiledView`) | one `QGraphicsView` paint |
| `File::setFile` / `File::fileChanged` | packets in, notifications out |
| `ChatModel::formattedRow.miss` | chat rows formatted from scratch |
| `MessageModel::formattedRow.miss` | messages formatted from scratch |
| `ChatModel::sortChats.layout` | sorts that actually signalled the view |

Raw logs from this session are not in the repo. Their headers carry the arm, scenario and
test chat; reproduce by rerunning the gestures above.
