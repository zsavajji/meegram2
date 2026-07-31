# Profiling: what the scroll work actually bought

Measured on device, 2026-07-31. `docs/features.md` said the performance work was
"implemented; none of it has been measured" — this is the measurement. It settles three
of the five changes on the scroll path, corrects the reasoning behind a fourth, and finds
that the thing dominating chat-list scrolling is none of them.

**One arm, one run per scenario.** The plan called for a baseline arm and a median of
three runs; neither happened, because the session ran out of runway on a blocked scenario
(see [What is not measured](#what-is-not-measured)). Every number here is arm B — current
`main` — and every verdict below rests on **ratios within a single run**, which is what
the counters were designed for: instrument the branch and the total, and the `calls` ratio
is the answer without a baseline binary.

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

**Restart `meegramd` with the app.** A UI starting against a *resident* daemon also gets
an empty chat list: the daemon's TDLib already emitted its `updateNewChat` burst to the
previous UI, and `loadChats` (`ChatModel.cpp:215`) answers 404 rather than re-sending. So
every run here begins:

```sh
killall meegram meegramd; sleep 3; killall -9 meegram meegramd
# then launch with the session bus borrowed from the running desktop:
p=$(pidof meegotouchhome)
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$p/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p" | head -1)
export DISPLAY=:0
/opt/meegram/bin/meegram 2>&1 | tee /home/user/prof-<arm>-<scenario>-<run>.log
```

That the daemon surviving the UI leaves the next UI with an empty list is a **defect**,
not a profiling artefact — surviving the UI is the daemon's whole purpose. It is not
fixed here.

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
| Photos | 50 | 23 | **0.46** |

Same mechanism, opposite outcome, and the explanation is file size: a photo produces
intermediate progress packets in which nothing exposed moves, and those are suppressed. An
avatar is small enough that every packet it produces *is* a state transition.

`MessageModel::data` in the same run: 449 calls, 27.6 µs avg, 44 misses (**9.8%**), miss
167 µs — about 2× the chat model's warm cost per call, and clear of the noise floor.

## Verdicts

| change | verdict | evidence |
|---|---|---|
| **#1** `MessageModel` row cache | **Partially confirmed.** 90.2% hit rate, hits well above the noise floor at 27.6 µs | Twongo run. The `m_formatted.clear()` suspicion (`MessageModel.cpp:785`) is **untested** — it needs history paging, which is broken |
| **#2** RichText → PlainText | **Not measured.** Needs the arm-A build and a working message-list scroll | — |
| **#3** preview newline strip | **Confirmed, and free.** Folded into `ChatModel::data`, which reads 5.9 µs warm — the floor | S1b |
| **#4** `File` emit guard | **Keep the code, fix the comment.** 0.46 on photos is a real win; 1.00 on avatars means it cannot help the case its own comment claims | S2 + Twongo |
| **#5** `sortChats` skip | **Better than the plan expected.** Swallowed 19 of 23 sorts during sync; the 4 that got through cost 75–296 ms *each*, inside the emit — that is the ListView relaying out | S1, go/no-go |

On **#5**: the plan predicted an idle account would prove nothing, and for steady-state
traffic that holds — zero sorts fired during either scroll window. But the startup sync
burst measured it by accident, and a suppressed `layoutChanged` is worth ~100 ms of view
relayout on this hardware. A near-zero reading on an idle account remains **not evidence
either way** for the busy-account case.

## What this found that the plan was not looking for

1. **Avatar decode dominates chat-list scrolling** — 82 ms per first display, 7.6 s per
   60 s of scrolling, against 43.8 ms for the entire model layer. Every remaining scroll
   optimisation on the C++ side is rounding error next to this. Decode at display size,
   or decode fewer, or decode off the critical path.
2. **`Utils::replaceEmoji` runs per display, outside the cache.** `ChatItem.qml:71` calls
   `utils.replaceEmoji(model.title)` as a QML binding, so it re-runs on every delegate
   rebind: 233 calls and **88.5 ms** during a warm re-scroll where the C++ cache took zero
   misses. That is 9× everything `ChatModel::data` does in the same window. The cached row
   already holds `title`; storing the emoji-replaced form is the same change as #3, one
   field over.
3. **The GL viewport costs 9.1 MiB** (S0).
4. **The instrument's own floor is ~5 µs on this hardware**, which sets the resolution
   limit for every future run.

## What is not measured

- **S4, message-list scroll** — blocked. The test chat would not page older messages, and
  the app then **segfaulted**. No core dump (`core_pattern` is `/dev/null`), no gdb on
  device, nothing in syslog or dmesg. Both need a debug build and their own session.
  Change #2's verdict is behind this.
- **Arm A (baseline `249b0b0`)** — never built. Not needed for #3, #4, #5, whose verdicts
  are within-run ratios; needed for #2.
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
