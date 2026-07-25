# Features

What MeeGram can do, what it deliberately cannot, and where each piece lives. Every
entry here has been built and is in the tree; the **Status** column says whether it has
been confirmed on real hardware.

Read this alongside [Architecture](/architecture) for how the pieces connect, and
[Troubleshooting](/troubleshooting) for the rough edges.

---

## At a glance

| Area | Works | Not implemented |
|---|---|---|
| Chats | list, folders, archive, pin, mute, mark read/unread, delete/leave | drafts, search, chat creation |
| Messages | text with entities, replies, edit, copy, delete, read receipts | forward, search, pinned messages, reactions |
| Media | photo receive + send, animated and static stickers | video, voice, documents, location |
| Presence | typing indicators, online status, connection state | sending your own typing action |
| System | notifications with avatar, tap-to-open, D-Bus activation | background operation while closed |

---

## Chat list

**Loading is demand-driven.** `refresh()` asks TDLib for one page via `loadChats`;
reaching the bottom of the list calls `ChatModel::loadMore()` for the next. QML1's
`ListView` has no `fetchMore` of its own — that is a Qt Widgets view API — so the
demand signal comes from `onAtYEndChanged` in `ChatListView.qml`. Everything the model
holds is visible (`revealAll()`); a `ListView` only builds delegates for rows in view
plus its `cacheBuffer`, however large `rowCount` is.

**Membership** is "the chat has a position for this list". TDLib documents an order of
`0` as "not in the list", but in practice only pinned chats arrive with a real order, so
treating `0` as absent hides almost everything — see
[Troubleshooting](/troubleshooting).

**Sorting** is decorate-sort-undecorate, keyed on the position order. The projection
costs a `weak_ptr::lock()` plus a scan of the chat's positions, so computing each key
once turns `O(n log n)` locks into `O(n)`.

### Chat actions

Long press a row for a Harmattan `ContextMenu`:

| Action | Request |
|---|---|
| Pin / Unpin | `toggleChatIsPinned` with this model's chat list |
| Mark as read | `toggleChatIsMarkedAsUnread(false)` **and** `viewMessages` if there are real unread messages — the manual flag and unread messages are independent |
| Mark as unread | `toggleChatIsMarkedAsUnread(true)` |
| Mute / Unmute | `setChatNotificationSettings`, setting only `mute_for` and leaving every other field on the scope default |
| Delete | `deleteChatHistory(remove_from_chat_list)` for private and secret chats, `leaveChat` for groups and channels |

Deletion needs both requests because TDLib has neither one that covers both cases:
private chats cannot be left, and groups cannot be cleared out from under their other
members. `revoke` is deliberately `false` — wiping the other side's history is a
separate decision.

::: warning Deleted chats are remembered for the session
TDLib keeps sending updates for a deleted chat and its stored position still names the
list, so `ChatModel` keeps a `m_removedChatIds` set to stop the next update putting the
row back. It is never emptied, so a private chat you delete stays hidden until restart
even if the other side writes again. Notifications still fire.
:::

---

## Messages

**Text** renders as `Text.RichText`, because `Utils::formattedText` converts TDLib
entities — bold, italic, code, links, mentions, hashtags — into HTML. Emoji become
`<img>` tags pointing at the bundled PNGs.

**Replies** show a quote block above the message: accent bar, sender, one elided line of
preview. `Message::ReplyInfo` is plain data rather than a `QObject`, because the strings
QML needs require a `StorageManager` and a `Locale` to produce, so `MessageModel`
formats them into roles. TDLib only fills the origin fields when the replied-to message
came from elsewhere; for an ordinary same-chat reply the model falls back to looking the
message up among those it has loaded.

**Scroll behaviour** has three owners that hand off explicitly, so they cannot fight
over `contentY`:

- opening a chat pins to the unread marker, or the newest message when nothing is unread
- your first drag takes over
- a live message hands control to the follow-the-newest flag

Opening re-asserts position on a **bounded** timer — five passes at 60 ms. Driving it
from `onContentHeightChanged` instead hangs the app: repositioning builds delegates
whose real heights differ from `ListView`'s estimate, which moves `contentHeight`, which
repositions again, and with a `cacheBuffer` rebuilding rows either side it never
converges.

### Message actions

Long press a bubble:

| Action | Notes |
|---|---|
| Reply | Fills the composer's reply banner |
| Copy | `Utils::copyToClipboard` — QML1 has no `Clipboard` element |
| Edit | `editMessageText` for text, `editMessageCaption` for a photo — TDLib rejects the former for anything that is not a text message |
| Delete | `deleteMessages`, with `revoke` for your own messages |

Items hide rather than grey out, which is what Harmattan does. Edit is offered on your
own messages only.

### Read receipts

`viewMessages` is reported for the **bottom-most visible** message. Inbox read state is
a single "last read" pointer, so that marks everything above it read too — which is why
one id is enough.

`source` is set to `messageSourceChatHistory` explicitly. Left null it means "guess from
the chat's open state", and that guess depends on `openChat` having been processed
before the view is reported.

Reporting happens on scroll-stop, on each settle-timer tick after load, and when a
message arrives while you are at the bottom. The settle-timer path is the one that
matters: `onCountChanged` fires before the delegates exist, so `indexAt()` fails there.

::: tip Opening a chat does not clear the whole badge
Only what is on screen is reported, so a chat with 30 unread clears as you scroll. Use
**Mark as read** from the chat-list menu to clear it in one call.
:::

---

## Media

### Photos

**Receiving** picks the smallest size that still covers the 480px screen, falling back
to the largest — downloading a 1280px original to draw it at 380 wastes bandwidth and
decode time. The rule is a pure `constexpr` with `static_assert`s in
`MessageContent.cpp`, because TDLib does not guarantee the sizes arrive sorted.

The bubble reserves its final geometry from the metadata, so it does not resize under
your thumb when the download lands; a scrim and spinner fill it meanwhile.

No image provider is involved — `Image` with `sourceSize` and `asynchronous: true` gives
Qt's own scaled decode off the GUI thread plus QML's built-in pixmap cache.

**Sending** goes through the gallery picker: `DocumentGalleryModel` from
`QtMobility.gallery`, which is the same Tracker index the stock Gallery uses, so it sees
the camera roll without walking the filesystem. Thumbnails use `smooth: false` — on the
SGX530 a smooth scale over a grid costs more than it buys.

The send is `inputMessagePhoto` wrapping an `inputPhoto` with an `inputFileLocal`.
Dimensions come from `QImageReader::size()`, which parses only far enough to find them
rather than decoding an 8 MP shot. Whatever is in the composer rides along as the
caption.

::: warning Downloads on sight
A delegate only exists for rows in view plus the cache buffer, so this is "what you
scrolled to" rather than "the whole history" — but it ignores whether the connection is
metered. Marked `ponytail:` in `MessageDelegate.qml`; wire it to Telegram's
auto-download settings if it bites.
:::

### Stickers

Three formats, three outcomes:

| Format | Rendering |
|---|---|
| `tgs` (animated) | **rlottie**, via the existing `LottieAnimation` item. Plays once, as Telegram does |
| `webp` (static) | **libwebp**, via `StickerProvider` |
| `webm` (video) | Falls back to the emoji — VP9 on an SGX530 is not worth a decoder |

Animated stickers came almost free: rlottie was already linked for the onboarding
animations, and `LottieAnimation` already gunzips `.tgs` (`inflateInit2` with `15 + 16`)
and accepts a `file://` URL.

`StickerProvider` is a `QDeclarativeImageProvider` rather than a `QImageIOPlugin`,
because WebP only ever arrives here as a sticker, and a static plugin that fails to
register reports itself as "unsupported format" with nothing to debug. Two details
matter on this hardware:

- **Scaled decode.** `WebPDecoderConfig.options.use_scaling` means a 512×512 sticker
  never expands to its full 1 MB to be drawn at 180px.
- **`MODE_bgrA` into `Format_ARGB32_Premultiplied`.** BGRA is ARGB32's byte order on
  little-endian, so it decodes straight into the `QImage` with no channel swap, and
  premultiplied is the form Qt's raster engine composites without converting. Stickers
  are mostly transparent, so that blend runs on every frame.

The cache is byte-budgeted rather than count-based, so one large sticker cannot blow it
open. Only drawable formats are downloaded.

### Avatars

Chat photos are decoded at the requested size and **masked in C++**. This used to be a
`MaskedItem` plus a second `Image` holding the mask in every delegate, running the
cutout live per row; `ChatPhotoProvider` now composites once per avatar and caches the
result, so the delegate is a plain `Image`.

The crop moved with it: the delegate used `fillMode: PreserveAspectCrop`, so the mask
was previously applied to an already-square image. The provider scales with
`KeepAspectRatioByExpanding` and centre-crops before compositing — masking a non-square
decode would clip the circle.

::: warning The placeholder is not masked
`icon-l-content-avatar-placeholder` comes from the theme, not the provider, so nothing
rounds it. Harmattan's contact placeholder should already be avatar-shaped.
:::

### Emoji

Colour PNGs, deliberately — a font would mean monochrome, because Noto Color Emoji is
CBDT/CBLC and Qt 4.7's `QFontEngineFT` rasterises only to `Format_A8`/`Format_Mono`.

The assets are 32×32, down from 64×64, which took the set from **19.6 MB to 7.8 MB**.
They are drawn at 24px, so the originals carried 7× the pixels ever displayed and Qt
downscaled every one on every draw.

`Utils::replaceEmoji` builds its lookup table lazily with a `canStart[256]` first-byte
filter and probes only key lengths that exist, so text with no emoji costs one indexed
pass and returns the original string.

---

## Presence

**Typing indicators** come from `updateChatAction`. `StorageManager` emits
`chatActionUpdated`, and `ChatInfoFormatter` shows it in place of the last-seen line —
prefixed with the sender's short name in groups, where it matters who. Recording and
uploading actions get their own strings.

It expires after 6 seconds on its own. TDLib repeats the action while it continues and
sends a cancel when it stops, but a client that missed the cancel would otherwise show
"typing" forever.

**Connection state** covers all five `ConnectionState` variants. An unmapped state logs
a `qWarning` rather than silently leaving the previous string in place, which is how a
header could sit on a stale "Connecting" with nothing to indicate why.

::: info Your own typing is not sent
`sendChatAction` is not wired up, so others do not see you typing. Display only.
:::

---

## Notifications

One notification per chat, updated in place as more messages arrive, withdrawn when you
open the chat or read it elsewhere. That is what the platform's grouping expects, and
what a phone wants over one banner per message.

**Transport is D-Bus**, not libmeegotouch: QtDBus is already a dependency and
`MNotification` is a thin wrapper over the same calls. Service
`com.meego.core.MNotificationManager`, path `/notificationmanager`, event type
`x-nokia.messaging.im`.

**Suppression** is a chain of one-liners: not outgoing, not a service message, not
muted, not the chat on screen, not already read, and **not older than app start** —
without that last one, launching after a busy night raises one banner per chat.

**The avatar** is only handed over once `isDownloadingCompleted()` — TDLib fills in the
local path when a download *starts*, and pointing the notification manager at a
half-written file draws a broken red square. It is a plain path, not a `file://` URL:
`MNotification` takes an image id or a filesystem path. When no avatar has been
downloaded yet the notification asks for one, so the next one from that chat has it.

**Tapping** raises the app and opens that chat. The chat id travels in the **D-Bus
object path** (`/chat/n1001234567890`) rather than as an argument, because the action
string's argument encoding is undocumented while a path is a plain string either way.
A `com.meegram.service` file under `/usr/share/dbus-1/services` lets D-Bus start the app
if you had closed it.

::: warning Only while the app is running
Harmattan keeps backgrounded apps alive, so "minimised" is covered — "closed" is not.
TDLib locks its database directory, so a separate always-on daemon cannot coexist with
the app; it would mean proxying the whole update stream over IPC. Autostarting the app
is the cheap answer. There is also no push channel for an N9: no Play Services, and
Nokia's notification servers are long gone.
:::

---

## Not implemented

Honest list, so nobody goes looking:

- **Forwarding.** Needs a chat-picker page plus `forwardMessages`.
- **Voice notes.** Needs QtMobility's `QAudioRecorder`, which is not linked and may not
  be in the sysroot, and Telegram wants OGG/Opus so a transcode may be required.
- **Video, documents, location, contacts, polls.** The content classes exist and the
  chat-list preview names them, but there is no delegate — they render as "not
  supported".
- **Search, drafts, chat creation, reactions, pinned messages.**
- **Sending your own typing action.**

---

## Performance work

All of it is implemented; **none of it has been measured**. `src/ScopeTimer.hpp`
instruments `ChatModel::data`, `sortChats`, `Locale::getString` and
`ChatPhotoProvider::requestImage`, and `-DMEEGRAM_PROFILE=ON` has never been run.

What is in:

- `replaceEmoji` rewritten from `O(text × 3773)`
- a memo cache on `Locale::getString`, which backs every `tr()` in the process
- `ChatModel` id-to-row index, replacing a linear scan that locked a `weak_ptr` per element
- a `FormattedRow` cache for the three roles that cost a formatter call, because QML1
  has no per-row role cache and a delegate reading eight properties calls `data()` eight
  times
- demand-driven chat loading instead of loading every chat at startup
- a TDLib update leak fixed in `Client`
- avatars decoded at requested size and masked once
- `cacheBuffer` cut to half a screen either side
- emoji assets at the size they are drawn

`-DMEEGRAM_GL_VIEWPORT=OFF` remains an untested one-flag A/B. Qt 4's `QGraphicsView`
cannot do partial updates with a GL viewport, so on a mostly-static chat list it may
well be slower than software paint. Nobody has checked.
