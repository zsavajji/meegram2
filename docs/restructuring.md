# Restructuring: TDLib as a service

A design note that has since been partly built. **What is built:** `meegramd` owns TDLib
and relays it over a Unix socket (`src/daemon/main.cpp`), and it posts the system
notifications itself (`src/daemon/Notifier.cpp`). **What is not:** `Authorization` still
runs in the UI, so a first login needs the app open. See "Sequencing" at the end for
what each step actually landed as — that list is the status, and it is kept honest.

**The goal.** The app used to have to stay resident to receive messages — TDLib lived in
the UI process, so closing the window closed the connection. The intended end state is
a service that owns TDLib and the database, and a UI that is only a front end to it.
The service outlives the UI; notifications keep arriving with no window open.

Read `architecture.md` first. This note assumes the `Client` seam and the
`StorageManager` fan-out described there.

---

## What the socket actually costs

Worth settling before designing anything, because the intuition is usually wrong.

An idle TCP connection costs a few KB of kernel buffers. It is not the expense. The
expense of staying connected is:

| cost | does a service reduce it? |
|---|---|
| Radio wakeups for MTProto pings | **No** — identical, the connection exists either way |
| TDLib RSS: message DB, chat info DB, update handling | **No** — same work, relocated |
| Qt + QML engine, scene graph, pixmap and avatar cache | **Yes** — this is the entire win |

So the service does not buy less network or less TDLib. It buys **not holding the UI
resident**. On a 1 GB device that is a real win, and on this codebase plausibly a large
one — 3,770 emoji PNGs (~20.5 MB) are compiled in, `ChatPhotoProvider` and
`StickerProvider` both cache decoded pixmaps, and QML1 rasterises on the CPU with no
scene graph to hand memory back.

~~**It is an assumption until measured.**~~ **Measured on device, 2026-07-31.** N9,
`-DMEEGRAM_PROFILE=ON`, real account, warm database, run over SSH. The `MEEGRAM_RSS`
markers in `main.cpp` straddle the Qt / QML / TDLib boundaries so consecutive deltas
attribute the resident set.

| marker | RSS | delta |
|---|---:|---:|
| `qt-app` — `QApplication` up | 10,524 KiB | +10.3 MiB |
| `view+gl` — `QDeclarativeView` + `QGLWidget` | 15,284 KiB | +4.6 MiB |
| `tdlib-client` — `Client` constructed | 15,524 KiB | **+0.2 MiB** |
| `qml-scene` — after `setSource` | 44,108 KiB | **+27.9 MiB** |
| steady state, ~2 min after sync | ~80,050 KiB | +35.1 MiB |

**Building the QML scene costs 27.9 MiB in one step** — more than Qt, the view and the
GL widget combined (15.0 MiB). TDLib's client object is 0.2 MiB, though its databases
open later.

The closing +35.1 MiB is the one figure these markers cannot split: TDLib opening and
syncing its databases, and QML decoding avatars and emoji into pixmap caches, growing in
the same process at the same time.

`MEEGRAM_HEADLESS=1` (`main.cpp`) splits it — it drives TDLib to a full sync without ever
building the QML scene. Same device, same account, same session. It plateaus at
**39,900 KiB** and stays there (flat from t+65 s to t+170 s, ±20 KiB). The startup
markers reproduce the run above to within 4 KiB, so the two runs are comparable:

| component | RSS | share |
|---|---:|---:|
| Qt + `QDeclarativeView` + `QGLWidget` | 14.9 MiB | 19% |
| TDLib — client, databases, sync, `NotificationManager` | **24.0 MiB** | 31% |
| QML scene + pixmap/emoji caches | **39.2 MiB** | 50% |
| total, steady | 78.2 MiB | |

Derived as: TDLib = headless steady (39,900) − the `view+gl` marker (15,288); QML =
full steady (~80,050) − headless steady. The scene itself is 27.9 MiB of that 39.2, the
remaining 11.3 MiB being avatars and emoji decoding into caches over the first minutes.

**The UI outweighs TDLib 2.25 : 1.** Evicting it reclaims 54.1 MiB of 78.2 MiB — 69% —
and leaves a 24 MiB resident daemon. The premise holds with room to spare; the earlier
bound of ≥55% was pessimistic.

One thing the headless run gives away for free: `NotificationManager` runs in it, and its
"skipped — in the store but not in any chat list" lines appear exactly as in the full
app. So **24 MiB is not a hypothetical daemon floor, it is a measured one** — that
process is very nearly what the service would be, minus the socket.

### Minimising returns nothing

The other half of the reading, and it changes one of the options below.

| | RSS |
|---|---:|
| foreground, steady | 81,008 KiB |
| minimised, +30 s | 85,088 KiB |
| minimised, +60 s | 85,888 KiB |

Backgrounding the window **costs 4.9 MiB and releases none**. Confirmed genuinely
backgrounded, not merely idle in front: 1 CPU tick per 15 s and no instrumented scope
firing at all. The rise is the compositor's task-switcher thumbnail plus the transition;
the slow creep after it is TDLib taking updates. QML1 holds the entire scene while
hidden, which is exactly the ~43 MiB the service architecture exists to reclaim.

Consequence for "the cheap alternative" below: it does **not** come free from minimising.
Whatever memory it recovers has to be recovered by explicitly tearing the
`QDeclarativeView` down, because the platform gives back nothing on its own.

### Tearing the scene down does not work either

Built and measured (`MEEGRAM_KEEPALIVE=1`, `main.cpp`): keep the process alive on window
close, drop the scene with `setSource(QUrl())` + `clearComponentCache()` +
`QPixmapCache::clear()`, then `malloc_trim(0)`.

| stage | RSS | delta |
|---|---:|---:|
| steady, window open | 79,696 KiB | |
| window closed, before teardown | 70,844 KiB | −8.6 MiB |
| after dropping the whole scene | 70,896 KiB | **+52 KiB** |
| after `malloc_trim(0)` | 69,968 KiB | **−928 KiB** |
| *(a fresh headless process)* | *39,900 KiB* | |

**Destroying the entire 28 MiB QML scene returns 52 KiB.** The 8.6 MiB that does come
back arrives when the window closes — the compositor reclaiming the GL surface — and
would happen anyway.

`malloc_trim` was the test for whether this is merely allocator retention: freed into
glibc's arenas but never handed to the kernel, which would make it a one-line fix. It is
not. Trim shrank the main arena's brk region from 23.9 to 18.2 MiB and its resident pages
from 23,440 to 16,836 KiB — and total RSS moved 928 KiB, the process taking the pages
straight back elsewhere. The memory is **live and referenced**, not free-and-retained.

Qt 4.7 offers no public way to release it. `QDeclarativePixmapStore` holds every decoded
avatar and emoji with no flush API; the QtScript JS heap has no reachable
`collectGarbage`; `trimComponentCache` is Qt 5. So a process that has once built the
scene stays ~30 MiB above a process that never built it, permanently.

**This is the finding that decides the architecture.** In-place teardown cannot reach the
headless floor. Only replacing the process image can — which is the service
architecture's core claim, now measured rather than assumed.

---

## Why the split goes below `StorageManager`, not around it

The obvious reading is "move `NotificationManager` out". It does not work.
`NotificationManager` is a leaf that reads `StorageManager`, so a notifier daemon
would need its own TDLib — and TDLib takes an **exclusive lock** on its database
directory:

```cpp
AppManager.cpp:328   database_directory_      = ~/.meegram/tdlib
AppManager.cpp:330   use_chat_info_database_  = true
AppManager.cpp:331   use_message_database_    = true
```

Two processes cannot open the same account database. The alternatives — a second
database directory, or a second session on the account — mean double sync and double
battery for one banner.

So the ownership inverts: **the service owns TDLib; the UI becomes a client.** The
question is where to cut, and the answer is one level lower than it first appears.

`StorageManager` is referenced by 19 files and looks like the boundary. It is not.
`Client` is:

```
construction   AppManager.cpp:221                  — one site, one instance
outbound       send(td_api::Function, callback)    — ~30 call sites
inbound        signals: result(td_api::Object *)   — 4 subscribers
```

Request/response plus a broadcast update stream — already the shape of an IPC
boundary, because it is TDLib's own shape. Cut there and:

- the **service** runs what is today `Client::initialize()`, essentially unchanged
- the **UI's** `Client` is reimplemented as a proxy with the identical public interface
- `StorageManager`, `ChatModel`, `MessageModel`, `Utils` and every entity wrapper
  **compile unchanged**

That is the whole reason this is tractable. The app already funnels all TDLib traffic
through two members of one class.

---

## The hard part: the wire format

`td_api::Object *` is a C++ object tree. Getting it across a socket is the project;
everything else is plumbing. Three options, and they trade badly against each other:

| approach | app changes | cost / risk |
|---|---|---|
| **TDLib JSON** (`td_json_client`, `td_api::from_json`) | none in `src/`, but needs a codec TDLib does not ship | serialize + parse per update, continuously, on a ~1 GHz Cortex-A8 |
| **TL binary** (`td::tl::` internals) | ~~none~~ **does not exist** | — |
| **Narrow custom protocol** | large | smallest and fastest wire, but the `td_api` → model translation moves service-side, dragging `StorageManager` with it |

The first two keep the app untouched and pay at runtime. The third is fast on the wire
but relocates the model, which gives up the entire advantage of cutting at `Client`.

**This choice is the project.** On this hardware, prototype JSON first: it is the only
one that is both cheap to try and possibly fast enough, and measuring it answers the
go/no-go without committing to anything.

### Measured: JSON costs ~9% CPU. It is fast enough.

`tools/json_bench.cpp`, `-DMEEGRAM_JSON_BENCH=ON`, on device, 2026-07-31. The same
account run twice from an identical database snapshot with `db.sqlite*` deleted and
`td.binlog` kept — which forces a full chat/message resync without a re-login, and
produces ~88 updates/s rather than the ~5/s of a warm idle client. Two pairs:

| run | objects | CPU | µs/object | `json_decode` each |
|---|---:|---:|---:|---:|
| native ① | 8,290 | 46.50 s | 5,609 | — |
| native ② | 7,986 | 45.88 s | 5,744 | — |
| json ① | 7,631 | 46.98 s | 6,156 | 391 µs |
| json ② | 7,901 | 48.84 s | 6,181 | 427 µs |

**JSON costs 492 µs more CPU per update than native `td_api` objects — 8.7%.** The JSON
runs agree within 0.4% and native within 2.4%, so the gap is well clear of the noise.
Run ② ran with 50% packet loss and still landed within 0.4% of run ①: per-object CPU is
a ratio, so a degraded link costs throughput, not cost per update.

Breakdown of the 492 µs: ~409 µs is the `json_decode` parse, timed directly. The
remaining ~83 µs is TDLib's `to_json` plus the buffer copy — inferred by subtraction, and
the least certain figure here. Encode being much cheaper than parse is expected: encode
is a straight `StringBuilder` walk, parse tokenises, allocates and unescapes.

Socket bandwidth at that rate: 16.5 MiB per 90 s, **188 KiB/s peak**, mean object 2,142
bytes. Trivial for a Unix socket.

**Zero decode failures across ~16,000 real updates.** That is a correctness result as much
as a performance one — every message type this account actually produces round-trips.

Two caveats on the figure. The parse is measured but the field mapping that would follow
it is not, because `from_json(Object)` does not exist (see the correction above) — so the
true decode cost is higher than 409 µs, plausibly by up to 2×. Even doubling it leaves
JSON around 15% CPU overhead, which does not change the verdict. And outbound
`to_json(Function)` is unmeasured for the same reason; it matters far less against a
firehose of inbound updates.

**Verdict: performance is no longer the objection to the JSON transport.** The obstacle
is the missing client-mode codec, which is a build and maintenance problem, not a runtime
one.

### Correction: neither "app changes: none" row is true as written

Verified against the `td/` checkout at the pinned commit, not from memory.

**TL binary does not exist for `td_api`.** TDLib generates binary TL storers for the
MTProto layer only — `telegram_api.h` has 1505 `TlStorerCalcLength` sites, `td_api.h`
and `td_api.hpp` have zero. `td_api` objects carry `store(TlStorerToString &)`, a
one-way human-readable dump, and no `fetch`. There is no non-public API to reach for
here; the codec was never generated. This row is not a risk, it is a dead end.

**The JSON codec that ships is server-direction only.**
`td/generate/generate_json.cpp` invokes the generator once, with
`TL_writer::Mode::Server`. In that mode `gen_to_json` returns before emitting anything
for functions and `gen_from_json` skips result types
(`td/generate/tl_json_converter.cpp:88-163`), so `td_api_json.h` declares:

| shipped | what a UI-side proxy needs |
|---|---|
| `to_json(Object)` — encode updates and responses | `to_json(Function)` — encode outbound requests |
| `from_json(Function)` — decode incoming requests | `from_json(Object)` — decode inbound updates |

Exactly the mirror. That is correct for TDLib, whose only JSON consumer is
`td_json_client` — it receives functions and emits objects, and never needs the other
direction. It is the wrong half for us: the *service* is fine on public API alone, and
the *UI* has neither direction it needs.

The generator does support `Mode::Client`, which emits precisely the missing pair, so
the path exists.

~~But it means patching TDLib's generator... That is a TDLib fork to carry across
upgrades.~~ **Wrong — no fork is needed.** Implemented 2026-07-31; the cost was one new
file, `tools/generate_json_client.cpp`, which is `td/generate/generate_json.cpp` with the
mode flipped and the output renamed. `gen_json_converter()` is already public API in
`td/generate/tl_json_converter.h`.

**Correction: two lines in `td/` do have to change after all.** The generator has no
`to_json` for `vector<bytes>` — it emits the literal placeholder
`UNSUPPORTED STORED VECTOR OF BYTES` (`tl_json_converter.cpp:65`), which is not valid
C++ and fails the build. Upstream never trips it because server mode only emits
`to_json` for `Object`s, and no `Object` has such a field; client mode emits `to_json`
for queries, and two of those do — `inputPassportElementErrorSourceFiles` and
`...TranslationFiles`, both `file_hashes:vector<bytes>`. The placeholder is dead code
upstream, so this is an unimplemented case rather than a case we broke, and there is
nowhere outside `td/` to fix it.

The patch is two files: `td/tl/tl_json.h` gains a `JsonVectorBytes` (base64 per element,
mirroring the scalar `bytes` path and the existing `JsonVectorInt64`), and
`tl_json_converter.cpp:65` emits it instead of the placeholder. Nothing needs a new
include — the generated files already include `tl_json.h`. So the claim above should
read *nearly* untouched: two lines, in a spot upstream never exercises, against the
alternative of a real fork.

Not the only local patch in `td/` — `TD_HAS_MMSG` is forced to 0 in
`tdutils/td/utils/port/config.h` for Harmattan. Both are applied by
`tools/setup-dependencies.sh` (`patch_td_json_vector_bytes`, called from `build_tdlib`
next to the `TD_HAS_MMSG` step), idempotently, so a fresh checkout or a `TDLIB_COMMIT`
bump reapplies them without anyone remembering to. Neither is a fork: `td/` stays at the
pinned commit plus two lines, applied by script and re-derivable from it.

Both patches fail loudly rather than silently no-op if their anchor is gone, on the
theory that after a bump "no match" means upstream moved, not "already applied". For the
JSON patch that means: no `struct JsonVectorInt64 {` in `tl_json.h`, or neither the
placeholder nor `JsonVectorBytes` in the converter. The second case is the interesting
one — it is what upstream *implementing* `vector<bytes>` would look like, at which point
the patch should be deleted rather than repaired.

What made that possible is where the broken trailer lands. `gen_json_converter_file()`
emits it only when `file_number == 0`, and `is_suitable(0, 10, counter)` tests
`counter % 9 == -1`, which is never true — so **file 0 carries the trailer and nothing
else**. Verified against the generated output: `td_api_json_client_0.cpp` is exactly the
three trailer functions, and files 1–9 hold all 1,841 per-class functions. The build
compiles files 1–9 and skips file 0; `src/JsonCodec.cpp` hand-writes the three
replacements, of which the only non-trivial one is a three-line `downcast_call` over
`Function` (`td_api.hpp:6511` already generates that switch).

One wrinkle worth recording, because it is the thing that would push someone back toward
`Mode::All`. `from_json(object_ptr<Object> &)` dispatches through `downcast_call(Object &)`,
which enumerates *every* `Object` subclass — including query-only types that client mode
gives no decoder, so the instantiation fails to compile. Those types are unreachable
inbound by construction: they are function arguments, they travel UI → daemon only.
`Mode::All` generates both directions for everything and fixes it, at **2.06 MB of
compiled C++ against client mode's 1.22 MB** (measured) — 840 KB of decoders that cannot
run. A single catch-all template overload in `src/JsonCodec.hpp` costs three lines
instead; the generated per-class functions are non-templates and win overload resolution
outright, so it only ever binds the types that have none.

Consequence for sequencing: ~~there is no cheap in-process measurement~~ — moot. The
codec exists, and `tools/json_bench.cpp` had already answered the go/no-go from the
`json_decode` side alone.

### Transport

D-Bus is right for the control plane and wrong for the data plane — a broker
round-trip and a copy per message, plus size limits, on what is a firehose during
sync. Use a **Unix domain socket** for the update stream.

The control plane already exists: `NotificationManager` registers `com.meegram` and
handles tap-to-open through object paths (`architecture.md`, "Data flow: one
notification"). Service activation and wakeup belong there.

---

## What genuinely relocates

Not everything can be proxied.

- **`Authorization` moves service-side.** The service owns the database, so it must own
  the login state machine; the UI drives it over IPC. A real port.
- **`NotificationManager` moves service-side** — the point of the exercise. ~~But it
  reads `Chat`, `Message` and `Locale` to compose banner text, so the service needs a
  slim model of its own or must compose from raw `td_api`.~~ **Done**, and the premise
  was wrong in a useful way. The fear was that composing a banner needs so much of
  `StorageManager` that the whole cut at `Client` stops making sense. It does not,
  because the daemon does not drive off chat updates at all: TDLib has its own
  notification subsystem (`updateNotificationGroup`), which already applies the
  notification settings, ignores outgoing and service messages, and withdraws a
  notification when its message is read on any device. Nothing had ever switched it on —
  `notification_group_count_max` defaults to 0, meaning "this client does not show
  notifications" — so the update stream it feeds on did not exist to be found.

  What the daemon keeps of its own is three maps filled from updates it was relaying
  anyway: chat titles, user names, and chat photo paths. No requests, no responses to
  correlate, no model. `src/daemon/Notifier.cpp`, ~700 lines including the content
  preview switch, against 450 for the version in the app that could only run while the
  app did.
- **`Locale` is process-wide.** Every `tr()` in the process routes through
  `Locale::getString`. Both processes need one, fed from the same language pack.
- **Late-joining UI clients.** The service outlives any UI instance. TDLib's model is
  "ask again" (`getChats`, `getChatHistory`), so this should be fine, but the service
  can no longer assume a single lifetime-long consumer, and `m_handlers` in `Client` is
  currently keyed per request with no notion of *which* client asked.

### One thing that gets easier

`Client::disposeObject` exists because `result()` is queued and the update object must
outlive four subscribers' slots. Across a process boundary the UI owns its own
deserialized copies and the problem disappears — a rare simplification, and welcome
given that every segfault in this codebase so far has been object lifetime.

---

## Sequencing

The seam means this never needs a big-bang branch. Each step ships. Status is per step
and is the point of this list — "the restructure is done" is not a thing anyone should
have to infer.

1. **Make `Client` an interface** with two implementations, in-process and IPC. Ship
   the in-process one. Zero behaviour change, fully reversible. — **done**, one public
   `Client.hpp` over `src/Client.cpp` and `src/ClientProxy.cpp`, selected by
   `MEEGRAM_JSON_TRANSPORT`.
2. **Stand up the service** using the existing `initialize()` body verbatim. Nothing
   connects to it yet. — **done**, `src/daemon/main.cpp`, though not verbatim: the
   daemon relays `td_json_client` and the UI still sends `setTdlibParameters`.
3. **Prototype the wire format and measure on device.** Go/no-go. — **done**,
   `tools/json_bench.cpp`.
4. **Move `Authorization`.** — **not done.** The UI still owns the login state machine,
   so a first login needs the app open. Everything after it only assumes an authorized
   TDLib, which is why 5 could land first.
5. **Move `NotificationManager`.** The app stops needing to be resident — the point. —
   **done**, `src/daemon/Notifier.cpp`. The app keeps `NotificationEndpoint`, which is
   one D-Bus method for opening a chat when a banner is tapped, and no notification
   state at all.

What is left, in the order it matters:

- **Step 4**, above.
- **`MEEGRAM_KEEPALIVE` has outlived its question.** It keeps the process and drops the
  scene, to measure whether that returns the memory. It does not (see above), and with
  the daemon posting notifications the app no longer has any reason to stay: closing the
  window is now supposed to exit it. Left in place because it is one `if` in `main.cpp`
  and it is still the fastest way to reproduce the teardown numbers.
- **The daemon posts blocking D-Bus calls from the receive thread.** A wedged platform
  notification daemon stalls the relay for up to two seconds per banner. Its own thread
  would fix it; the `ponytail:` note in `Notifier.hpp` says so.

---

## The cheap alternative, for comparison

~~If the goal is only "do not hold the UI resident", most of the win is available with no
IPC at all: keep one process, tear down the `QDeclarativeView` on minimise, keep `Client`
and `StorageManager` alive, rebuild the scene on restore.~~

**Disproven — measured, see "Tearing the scene down does not work either" above.** The
teardown returns 52 KiB of a 28 MiB scene, and `malloc_trim` proves the remainder is live
rather than retained. The reasoning was sound and the premise was false: the QML engine
does not give the memory back, and Qt 4.7 exposes no API to make it.

### What replaces it: re-exec headless

The teardown result rules out *in-place* recovery, not single-process operation. Replacing
the process image does what freeing cannot — on window close, `execv()` the same binary
with `MEEGRAM_HEADLESS=1`.

That mode already exists and is already measured: TDLib syncs and `NotificationManager`
composes banners with no scene, at **39,900 KiB**. The database is on disk, so the new
image reopens it and carries on.

| approach | resident when closed | cost |
|---|---:|---|
| do nothing (today) | 78.2 MiB | — |
| in-place teardown | 69.9 MiB | disproven, and 8.6 of that is the window closing anyway |
| **re-exec headless** | **39.9 MiB** | ~5 lines in `main.cpp` |
| `meegramd` service | 24.0 MiB | ~~client-mode codec, TDLib fork, IPC, two processes~~ built — see below |

Re-exec captures **70% of the daemon's win for a fraction of a percent of the work**, and
needs none of the things blocking the JSON plan: no `to_json(Function)`, no patched code
generator, no second process, no `@extra` routing, no duplicated `Locale`.

**Superseded 2026-07-31: the service was built.** The "TDLib fork" that made this
comparison lopsided turned out not to be required — see the correction above — so the
service's cost fell to one generator invocation, `src/JsonCodec.{hpp,cpp}`,
`src/ClientProxy.cpp` and `src/daemon/main.cpp`. Re-exec remains the cheaper option and
is still the right answer if the service is ever backed out; it is left documented here
rather than deleted for that reason.

What it costs, honestly: TDLib disconnects and re-syncs across the exec, so there is a
window of seconds where a message would not raise a banner, and any in-flight request is
lost. Whether that gap matters is a product question, not an engineering one. It is also
strictly worse than the service on crash isolation — the exec is not a supervisor, so a
segfault still takes everything down.

The 16 MiB between re-exec and the service is what the entire transport project buys.
~~Decide whether that is worth a TDLib fork *before* starting one.~~ It needed no fork,
and the service was built.

---

## What shipped

| piece | file |
|---|---|
| client-direction codec generator | `tools/generate_json_client.cpp`, run by `tools/setup-dependencies.sh` |
| the three entry points the generator cannot emit | `src/JsonCodec.{hpp,cpp}` |
| the daemon | `src/daemon/main.cpp` → `meegramd` |
| the UI side of the socket | `src/ClientProxy.cpp`, same surface as `src/Client.cpp` |
| codec self-check | `tools/json_roundtrip.cpp` |

Selected by `-DMEEGRAM_JSON_TRANSPORT=ON`. Both transports stay buildable, so a
regression bisects to one flag.

Two decisions worth knowing about, because neither is in the original plan:

**meegramd does not parse the JSON it relays.** The plan called for per-connection
`@extra` namespacing so two UIs could not collide, which would mean decoding and
re-encoding every request in the daemon. Instead the UI prefixes its own `@extra` with
its pid, making it globally unique, and the daemon broadcasts every line to every
connection. A foreign response fails the prefix check in `Client::handleLine` and is
dropped — which is exactly what today's `Client` does with an unmatched request id. The
daemon stays pure plumbing with no routing table.

**The daemon is D-Bus activated.** `meegramd` claims `com.meegram.Daemon` with libdbus —
not QtDBus, which would put QtCore back into the process whose 24 MiB resident set is the
entire argument — and `resources/com.meegram.Daemon.service` lets dbus-daemon exec it on
demand. `Client::connectToDaemon` tries the socket first, calls `StartServiceByName` only
if that fails, and retries the connect across the gap.

That gap is deliberate. The name is claimed *before* the socket is bound, because the
name is the only real mutex between two daemons: `listenOn`'s unlink-then-bind cannot
arbitrate a race, since both instances would see a dead socket and the second would
unlink the first's and bind its own. So the name exists a few milliseconds before the
socket accepts, and the UI polls across it rather than trusting `StartServiceByName`'s
return.

`com.meegram.Daemon` and `com.meegram` are independent names despite the prefix; the UI
keeps owning the latter through `NotificationManager`.

**Attaching to an already-authorized daemon needs two things in-process never did.** Both
are assumptions that only hold when every launch starts a fresh TDLib, which is exactly
what the daemon stops being true:

- **TDLib announces authorization state only when it changes.** A UI attaching to a
  daemon already in `authorizationStateReady` is told nothing and sits on the login page
  in front of a live session. `AppManager::requestAuthorizationState` asks with
  `getAuthorizationState` and replays the answer through `Client::injectUpdate`, so
  `AppManager` and `Authorization` handle it on their normal `result()` path. Both are
  idempotent — `setState` guards on change, manager creation guards on `m_chatManager` —
  so the redundant replay on the in-process transport costs nothing and keeps the two
  behaving identically.
- **A second `setTdlibParameters` is an error, not a no-op.** It returns a plain 400
  "Unexpected setTdlibParameters", and `AppManager::setParameters` latched its
  initialization flag only on `ok` — so `appInitialized()` never fired and the app never
  started. That rejection means the parameters *are* set, which is all the flag records,
  so it now counts as success.

Neither is reachable without the daemon, and neither shows up on a fresh login — only on
the second launch, which is the case the whole project exists for.

### What the socket exposes

Moving TDLib out of the UI turns a private in-process object into an IPC endpoint that
speaks the whole `td_api` with an authenticated session behind it. `meegramd` checks
`SO_PEERCRED` on accept — same uid, and the peer's `/proc/<pid>/exe` must be the
`meegram` binary beside it — which stops another application from just connecting.

It does not make the same-uid boundary a security boundary, and nothing at this layer
could. That attacker can ptrace the UI and drive the socket from inside it, or ignore the
daemon and read `~/.meegram/tdlib` directly: the app never calls
`checkDatabaseEncryptionKey`, so the database is unencrypted and has always been readable
by any process at this uid. The honest framing is that the daemon converts *passive read
of the history* into *active control of the account*, and the peer check narrows who can
take that second step to processes that can already impersonate the UI.

Closing it properly means an aegis credential on the socket, which is Harmattan-specific
and not designed here.

---

## Open questions

- **Aegis credentials and autostart.** A session daemon needs its own package
  credentials and a way to be started and kept alive. There is already one unverified
  aegis credential question outstanding (`GRP::video`, see `troubleshooting.md`).
- **OOM behaviour.** Whether a Harmattan session service is spared the low-memory
  killer under pressure, or whether the service dies exactly when it is most needed.
- **Does the service need `use_message_database_`?** If the UI holds the message model
  and the service only needs enough to compose a banner, a lighter TDLib configuration
  service-side may cut the RSS that the whole exercise is trying to reclaim.
