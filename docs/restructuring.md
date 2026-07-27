# Restructuring: TDLib as a service

A design note, not a plan of record. Nothing here is built.

**The goal.** Today the app must stay resident to receive messages — TDLib lives in
the UI process, so closing the window closes the connection. The intended end state is
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

**It is an assumption until measured.** `ScopeTimer` exists
(`-DMEEGRAM_PROFILE=ON`); RSS before and after a minimise would settle whether the QML
side really dominates TDLib. Do that before committing to any of this.

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
| **TDLib JSON** (`td_json_client`, `td_api::from_json`) | none | serialize + parse per update, continuously, on a ~1 GHz Cortex-A8 |
| **TL binary** (`td::tl::` internals) | none | fast, but non-public tdutils API — fragile across TDLib upgrades |
| **Narrow custom protocol** | large | smallest and fastest wire, but the `td_api` → model translation moves service-side, dragging `StorageManager` with it |

The first two keep the app untouched and pay at runtime. The third is fast on the wire
but relocates the model, which gives up the entire advantage of cutting at `Client`.

**This choice is the project.** On this hardware, prototype JSON first: it is the only
one that is both cheap to try and possibly fast enough, and measuring it answers the
go/no-go without committing to anything.

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
- **`NotificationManager` moves service-side** — the point of the exercise. But it
  reads `Chat`, `Message` and `Locale` to compose banner text, so the service needs a
  slim model of its own or must compose from raw `td_api`. **Design this piece first**:
  it is the only one that cannot simply proxy, and it decides how much of
  `StorageManager` the service ends up needing. If the answer is "most of it", the case
  for cutting at `Client` weakens considerably.
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

The seam means this never needs a big-bang branch. Each step ships.

1. **Make `Client` an interface** with two implementations, in-process and IPC. Ship
   the in-process one. Zero behaviour change, fully reversible.
2. **Stand up the service** using the existing `initialize()` body verbatim. Nothing
   connects to it yet.
3. **Prototype the wire format and measure on device.** Go/no-go. Everything after this
   is wasted if serialization is too slow.
4. **Move `Authorization`.**
5. **Move `NotificationManager`.** The app stops needing to be resident — the point.

Step 3 is the decision point and is reachable in days, not months.

---

## The cheap alternative, for comparison

If the goal is only "do not hold the UI resident", most of the win is available with no
IPC at all: keep one process, tear down the `QDeclarativeView` on minimise, keep
`Client` and `StorageManager` alive, rebuild the scene on restore. All state already
lives in C++, and `NotificationManager` never touches QML, so banners keep working.

Costs a visible rebuild latency on restore, and needs verifying that the QML engine
returns the memory rather than fragmenting the heap. Contained to `main.cpp` and the
window lifecycle.

This is not the same thing — the service survives the UI *crashing*, and is the right
long-term shape — but it is the honest comparison, and it shares step 3's measurement.

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
