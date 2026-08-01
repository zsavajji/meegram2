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
| Chats | list, title search, folders, archive, pin, mute, mark read/unread, delete/leave, profile page | drafts, chat creation, message search |
| Messages | text with entities, emoji picker, big emoji, replies, edit, copy, delete for me / for everyone, read receipts, delivery ticks | forward, pinned messages, reactions |
| Media | photo receive + send with pinch-zoom view and save, animated and static stickers, voice notes recorded and played in-app, documents sent and received, video and GIF bubbles that hand off to the platform player | inline video playback, location, contacts, polls |
| Presence | typing indicators, online status, connection state | sending your own typing action |
| System | notifications with avatar, tap-to-open, D-Bus activation, resident daemon | — |

::: warning "Works" is not "plays"
Video and GIF messages have a **bubble**, not a player — the still, a badge, and a
hand-off to whatever Harmattan opens that type with. Nothing decodes video in this
process. See [Video and GIFs](#video-and-gifs).
:::

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

### Search

A field above the list, filtering on the **chat title** — substring, case-insensitive.
`ChatModel::filter` is a writable property, so each list (main, archived, every folder)
has its own filter and nothing downstream has to know searching exists: `matchesFilter`
sits beside `isInList` at the same three call sites.

Typing is **debounced** through a timer rather than filtering per keystroke, and clearing
the field stops that timer instead of restarting it — an in-flight debounce would
otherwise re-apply the filter you just cleared.

Setting a filter **re-scans from storage** rather than narrowing the rows already loaded:
widening or clearing it has to bring rows back, and storage is the only place that still
knows about them.

::: warning A search pulls the whole chat list in
The list normally pages 25 at a time, so an unqualified search would only ever see the
first screenful. While a filter is set, `handleChatsLoaded` chains the next batch until
TDLib reports the list exhausted. That is **unbounded** — an account with thousands of
chats loads all of them for one search. Marked `ponytail:` in `ChatModel.cpp`; the
upgrade is a `searchChats` request and a second model.
:::

Two things this cost, both now fixed and both worth knowing if you touch the caching:

- The cold-cache retry (`listExhausted && m_chats.empty()` → retry ten times at 500 ms)
  could not tell "the search matched nothing" from "TDLib knows of no chats yet", so a
  no-result search sat on a spinner for five seconds. It is gated on the filter being
  empty, which is the condition it was written for.
- `populate()` deliberately **no longer clears** `m_formatted` — it is keyed by chat id
  and invalidated per chat, and a search re-populates on every keystroke, so clearing it
  meant re-running `getChatTitle` and `replaceEmoji` (~380 µs a chat) for the whole list
  each time. The matching change is that `handleChatItem` invalidates **before** its
  visibility check: a chat filtered out is not in `m_rowById`, so invalidating only for
  visible rows left a stale entry to surface when the search was cleared.

::: info Titles only
Message text is not searched. That means a `searchMessages` call and a second model;
marked `ponytail:` in `ChatModel.cpp`.
:::

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
formats them into roles.

`messageReplyToMessage` only carries `origin` **and `content`** when the replied-to
message came from another chat. For an ordinary same-chat reply both are null, which is
why the quote block used to show a sender and no text at all. Both `replyToSender` and
`replyToText` fall back to looking the message up among those the model has loaded.

That fallback has a ceiling: a reply to a message too far back to have been loaded still
resolves to nothing. It fails cleanly rather than blankly — each line only takes space
when it has content, and with both empty the quote block hides itself and the bubble
reads as an ordinary message. `getMessage` plus a `dataChanged` when it lands is the
upgrade.

Positioning the quote for an **outgoing** message needs the same treatment stickers do —
see [the bubble layout note](#outgoing-content-is-not-right-aligned-for-free).

### Outgoing content is not right-aligned for free

The bubble is sided correctly by itself, but what goes **inside** it is positioned with
`anchors.left` plus a margin. The text delegate gets away with `leftMargin: 80` because it
spans the full width and sets `horizontalAlignment: AlignRight` — the *Label* starts at 80
but the glyphs land against its right edge. Nothing else does that:

- a **sticker** is only as wide as itself, so it sat at x=80, over on the incoming side,
  while its bubble hugged the right edge
- a **photo** survived by accident: `min(content.width, 380)` is usually exactly 380 in
  portrait, which happens to fill the span. A narrow portrait shot was misplaced too.
- a **reply quote** is an accent bar plus left-aligned labels, so it had the sticker's bug

Fixed-size content computes its offset instead, which reproduces the text convention
exactly — 16px of bubble padding on the left, 10px on the right, in both orientations:

```qml
leftMargin: model.isOutgoing ? listView.width - width - 20 : 20
```

The quote block derives its offset from the labels' `paintedWidth` rather than sizing the
block to its content, because those labels anchor to the block's right edge — making its
width depend on them is a binding loop. `leftMargin` feeds nothing, so it is safe.

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
| Save | Photos only, once downloaded — copies to `~/MyDocs/Pictures` |
| Delete | `deleteMessages` without `revoke` — this device only |
| Delete for everyone | `deleteMessages` with `revoke`, offered on your own messages |

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

### Delivery state

The other direction: a marker on your **own** bubbles, from the `sendState` role.

| State | Glyph | Source |
|---|---|---|
| Sending | `icons.sending` | `Message::isPending` |
| Failed | `icons.sendingerror` | `Message::isFailed` |
| Sent | `icons.check1`, washed-out white | `id > Chat::lastReadOutboxMessageId` |
| Read | `icons.check2`, `#7BE87B` | `id <= Chat::lastReadOutboxMessageId` |

Read is the one state that gets a colour of its own; the rest take the same washed-out
white the date label does, because the bubble behind them is `#15A8CA`.

Incoming and service messages return an **empty string**, which is what hides the marker
— the delegate tests the string rather than carrying a second "should this show" role.

Outbox read state is a single "everything up to here" pointer, the counterpart of the one
`viewMessages` moves on the inbox side, so `updateChatReadOutbox` repaints rows `0` to the
pointer rather than tracking which individual message flipped. The previous value is not
kept and rows already green re-read to the same string, so re-reading them is free; only
rows on screen re-read at all.

The marker widens the bubble and pushes the date label's right edge left, because the date
is right-aligned inside a label that spans the bubble — the tick cannot simply sit after
the text.

::: info updateMessageSendSucceeded is not optional
Sending a message puts it in the model **twice**: `updateNewMessage` delivers it at once
with a temporary id, and the server ack re-issues the same message under its real id. The
temporary one is never deleted — it is retired by that update and nothing else. Ignoring
it left the pending copy under an id no later fetch could match, so the next
`getChatHistory` saw the real message as new and appended it beside the copy already on
screen. `MessageModel::handleMessageSendCompleted` swaps the id in place, and re-sorts
only when a second message is still pending behind the first.
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

**Tapping** a photo opens `PhotoViewPage`, a black full-screen `Flickable` with pinch
zoom. Zoom goes through `Flickable.resizeContent()` rather than scaling the `Image`:
scaling leaves `contentWidth` untouched, so the pan limits stay at the unzoomed size and
most of a zoomed photo cannot be reached. The clamp is against the fitted width, because
`pinch.maximumScale` bounds only one gesture and successive pinches would otherwise zoom
without limit.

**Saving** writes the **original** size, not the one on screen. `MessagePhoto` keeps two
files: the size picked to cover the screen, which is what the bubble downloads, and the
largest size as sent, which is what Save wants. Both are registered with
`StorageManager`, or the original's download would never reach the object Save is bound
to; when a photo has only one size they are the same object, not two for one file id.
The original is normally not downloaded yet, so Save fetches it and completes when
`isDownloadingCompleted` flips — `fileChanged` also fires on progress, so completion is
checked rather than assumed.

The destination is `~/MyDocs/Pictures`, which is what puts it in the Gallery: that path
is under Tracker's index, whereas `QDesktopServices::PicturesLocation` resolves to
`$HOME/Pictures` on Harmattan and is not — the file would be written and never show up.
A file already present under that name is the same image saved earlier, so that counts
as success rather than a copy failure.

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

### Video and GIFs

::: danger Nothing plays inside the app
There is no video decoder in this process and no inline player. A video message is a
still, a badge and two taps: the first downloads, the second calls `Utils::openFile` and
hands the finished file to whatever Harmattan has registered for the type. If nothing is
registered, `openFile` returns false and the bubble says so rather than looking broken.
:::

`MessageVideo` and `MessageAnimation` carry a **deliberately identical property set**,
down to the names, so one delegate renders both and there is no second copy to keep in
step. A GIF is a soundless mp4 as far as TDLib is concerned; the two bubbles differ only
by the word on the badge.

**Only the still is fetched on sight** — a few KB, and without it the bubble is an empty
grey box. The video itself has no size ceiling and this is a metered radio, so it waits
for a tap. The frame takes its aspect from the metadata (4:3 when the video reports no
dimensions), so the bubble does not resize under your thumb when the still lands.

The still's format decides how it loads. `thumbnailFormat` is `"jpeg"`, `"png"` or
`"webp"`; the first two QML decodes itself, and **webp goes through `StickerProvider`** —
the same libwebp path a webp sticker takes. The provider is only called "sticker" because
stickers got there first. Its pixmap cache was widened to 8 MB to hold stills this size.

The badge reads `12:34 - 4.2 MB`, or `GIF - 1.1 MB` for an animation — Telegram marks a
GIF the same way, and its runtime of a couple of seconds says nothing useful. Both halves
come off the **message**, not the file, so the badge is right before anything has been
downloaded. The separator is conditional on there being two halves.

Long press saves it under **the sender's filename**, not TDLib's cache name.

### Documents

Name, size, and a circular icon that is a download glyph until the file has arrived and a
document glyph after. Full-bubble width regardless of the filename, which is how Telegram
draws a file row and saves measuring text to size the bubble. The name elides in the
**middle** — the extension is the part that says what the file actually is.

Same two-tap contract as video, and for the same reason: a document has no size ceiling.
First tap downloads, second opens it through the platform. Long press saves into
`~/MyDocs/meegram/Downloads` under the sender's filename, because TDLib's cache name is a
file id and means nothing to anyone.

::: warning There is no download progress, only a spinner
`File` exposes `canBeDownloaded`, `isDownloadingActive` and `isDownloadingCompleted` — no
byte count, no fraction, no percentage. Every download in the app therefore shows an
**indeterminate** `BusyIndicator` that replaces the icon glyph while it runs, not a
progress bar. `updateFile` carries the downloaded byte count, so a progress property is a
small change to `File` if a large document makes the spinner feel like a hang.
:::

`File::size` is **pre-formatted in C++** rather than being a number QML formats, because
td_api sizes are `int53` and must not cross into QML1 as numbers — the same rule that
applies to every id. See [Troubleshooting](/troubleshooting).

**Audio messages render through this same delegate.** `MessageAudio` keeps its file so
Save works, but nothing here plays an mp3 or an AAC — that is the platform's job, which
is exactly what the second tap hands it to.

**Sending** goes through `FilePickerPage`, browsing MyDocs with
`Qt.labs.folderlistmodel`. The version in the Harmattan 1.2 sysroot exposes only
`fileName`, `filePath` and `isFolder()` — there is no size or date role, which is why
rows show a name and nothing else. `ChatPage` keeps the page alive between attachments,
so closing it is really a hide; it resets its folder on `PageStatus.Inactive`, once the
pop animation is over, so the list does not visibly rewind on the way out.

### Voice notes

Recorded, encoded, decoded and played **in this process**. Harmattan has no opus — its
GStreamer is 0.10 and predates the elements, and libopus is not in the image — and
`inputMessageVoiceNote` is the only way to send a voice message, with OggOpus the only
format TDLib accepts for it. So libopus is linked statically and the container is muxed
by hand in `src/OggOpus.cpp`.

**Tap to start, tap to send.** Deliberately *not* hold-to-record: holding needs the press
to survive the notification banner, the lock button and everything else this device does
mid-gesture, and losing a recording to any of them is worse than one extra tap. While it
records, the attach button becomes **discard** — the attach menu is unreachable mid-record
anyway, and a recording with no way to throw it away is one you have to send by mistake —
and a counter runs beside it. Anything under a second is dropped as a mis-tap.

The bubble is the document row's three-state sibling: download, play, pause. One
`VoiceNote` instance per chat page drives capture *and* playback, so starting one note is
what stops whichever was already playing, with no bookkeeping. Its `source` property is
how a delegate tells whether it is the one making the sound without every bubble holding
a player.

**This is the one delegate whose file is fetched on sight** — a voice note is a few KB,
and the whole point is that you press play once rather than once to download and again to
hear it.

Details that are not obvious:

- **The microphone is opened at a rate libopus accepts** (16, 24, 48, 12 or 8 kHz, in
  that order of preference), not at whatever the device would rather hand over.
- **Encoding is incremental.** A minute of audio takes seconds to encode on a Cortex-A8,
  so `OggOpus::Writer` encodes as it captures; stopping does not stall on a whole-file
  pass, and a long recording is never held as raw PCM.
- **A read can end on half a sample**, and the odd byte belongs to the next one — hence
  the capture carry buffer.
- **Decoding is always at 48 kHz**, the rate the format is defined at. It decodes into
  one buffer, ~5.5 MB for a minute of mono; marked `ponytail:`, feed `QAudioOutput` page
  by page if notes ever get long enough to matter.
- **No waveform.** `waveform_` is left empty, which TDLib accepts. It is the little bar
  chart other clients draw behind the play button, and this one draws none.

::: info No Qt below the OggOpus API line
That is on purpose: the container muxing is the part most likely to be subtly wrong, and
keeping it to the standard library is what lets `tools/opus_roundtrip.cpp` — a known tone
through the encoder and back out of the decoder — build and run on a development machine
with libopus and libogg but no Qt 4.

```bash
cmake --build build --target opus_roundtrip && ./build/opus_roundtrip
```
:::

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

**A message that is nothing but one to three emoji** draws them large, the way Telegram
does: 32px for one, 24px each for two, 16px each for three, which keeps the line about as
wide either way. `Utils::emojiOnlySize` returns that size or `0`, and bails at the first
non-emoji character — so ordinary text pays almost nothing for the check. The table entry
holds both the pre-formatted 24px tag and a pointer into the static `Emoji::emojis()`
array, so off-default sizes format on demand without a second table.

`replaceEmojiSized` is a separate name rather than a default argument on `replaceEmoji`,
for the reason in [troubleshooting](./troubleshooting#q-invokable-with-a-default-argument).

**A single emoji is not a text message.** TDLib reports it as `messageAnimatedEmoji`,
which is why one on its own used to render as "unsupported". `MessageAnimatedEmoji`
exposes the same `text`/`formattedText` as `MessageText` so the same delegate renders it,
and `editMessage` treats it as text — it has no caption for `editMessageCaption` to edit.
The animated sticker it also carries is ignored: it is a tgs meant to play at sticker
size, and running rlottie for a 32px glyph buys nothing.

#### The picker

`EmojiPicker.qml` is a panel that takes the keyboard's place under the composer. It knows
nothing about where what it picks ends up — the caller owns the text field, and owns
lowering the keyboard before setting `open`, since only it knows which field.

**Seven tabs**, not nine: `Emotion` and `People` share one, and so do `Objects` and
`Symbols`. The icon font has no glyph for either second half, and Telegram groups them the
same way. A tab is an inclusive range over `Emoji::Category` rather than a list of ids,
because the categories are contiguous in `Emoji::emojis()` — a merged tab is two
neighbours joined.

::: info No recents tab
There is no most-recently-used row and no persistence. Categories only.
:::

`Utils::emojiCategory` returns `{ unicode, filename }` pairs and **leaves skin-tone
variants out** — they are 1875 of the 3773 entries, and a grid holding six of every person
is not one anything can be found in. The base emoji is still what gets sent; Telegram
renders it per the recipient's default.

Two costs it deliberately does not pay: `currentTab` starts at `-1` so the categories are
not built during construction — every chat page would otherwise pay for a panel most never
show — and the panel is `visible: false` when closed, which makes `Column` skip it
entirely rather than laying out a zero-height grid.

The grid is 48px cells on 32px assets, ten to a row in portrait. That is below the
platform's touch target, and it is a deliberate trade: a grid of full-size buttons fits
twenty emoji on screen and turns picking one into scrolling.

---

## Profile

Reached by tapping the avatar or the name in the chat header — one `MouseArea` over the
whole row rather than one on each, which also swallows taps that would otherwise fall
through to whatever is behind the header.

Shows the **large photo**, the title, the status line, and then whichever of these three
have anything to say: username, bio, phone number. Each is an empty string when it does
not apply — a group has no phone number, a user who hides theirs has none either — so the
page hides a row by testing its string rather than by knowing what kind of chat it is.

`Chat::bigPhoto` is the 640px variant, a **different file** from the avatar the chat list
uses, and nothing asks for it before this page opens. It is registered with
`StorageManager` alongside the small one, or its download would never reach the object the
page is bound to.

::: warning The bio needs a request, and the update alone is not enough
`loadUserFullInfo` sends `getUserFullInfo` and takes the answer from the **reply**, not
only from `updateUserFullInfo`. TDLib pushes that update only when the full info actually
*changed*, so a second request for the same user — which is what reopening a profile is —
would be answered by nothing at all.

The request lives on `StorageManager`, not on the caller: the reply arrives on the TDLib
worker thread, and `StorageManager` outlives every page that asks, while a
`ChatInfoFormatter` is destroyed the moment another chat is opened. The callback touches
nothing but the response and hands off through a queued `setUserBio`, so the map is only
ever written on the GUI thread.
:::

The page is pushed fresh each time and destroyed by the stack on the way back, like every
page here that is not a picker — there is no state on it worth keeping. It is bound to the
current selection rather than to a captured id, which is safe because it sits on top of
`ChatPage` and is popped before another chat can be opened.

The bio is kept as the string it is displayed as rather than behind a class of its own; it
is the one field of `userFullInfo` anything here shows.

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
muted, not the chat on screen *and* in the foreground, not already read, and **not older
than app start** — without that last one, launching after a busy night raises one banner
per chat.

"On screen" needs the foreground test because this client is left running so
notifications keep arriving, which means a chat can be open for days while the app sits
in the switcher. `isVisible()` is no use — Harmattan renders a live thumbnail of a
minimised app, so it stays visible; `isActiveWindow()` is what tracks attention. A banner
raised while minimised for the chat you have open is withdrawn by the "already read"
branch as soon as you return and the messages are marked read.

The **unusual** gates log why they stayed quiet — muted, older than app start, no
notification user id — because "no banner" on its own carries no information and cost
several builds of guessing. The steady-state ones are deliberately silent: `chatUpdated`
fires for read state, positions, settings and mention counts, so most calls land on the
dedupe or on "you are looking at this chat", and logging those buries everything else.

Order matters in one place: the already-read check runs **before** the dedupe, or a
message read elsewhere after a banner had already gone out would never have it taken down.

`publish()` treats a returned notification id of `0` as a rejection as well as an outright
D-Bus error, so a daemon that accepts the call and quietly declines to post it still
triggers the retry-without-image.

**The avatar** is a **plain absolute path**, not a `file://` URL — `MNotification::setImage()`
takes "a path to an image file or an icon id", and a URL draws the broken-image red
square. It is only handed over once `isDownloadingCompleted()`, because TDLib fills in the
local path when a download *starts* and a half-written file draws the same square.

::: warning Two symptoms that overlapped
This field was switched to `file://` once, on the reading that a bare path was refused and
took the whole banner with it. That was wrong. The missing banners were
`notificationUserId()` latching an early failure — one miss at startup, before the
notification daemon was up, disabled notifications for the whole session no matter what
this field contained. The image got the blame for a bug it had nothing to do with, and the
red square went unfixed for three builds as a result.
:::

When no avatar has been downloaded yet the notification asks for one, so the next one from
that chat has it — and `publish()` retries without the image on any rejection, because
decoration must never cost you the message.

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

- **Inline video playback.** Video and GIF messages have a bubble, a still and a hand-off
  to the platform player — nothing decodes video in this process, and VP9 on an SGX530 is
  not worth a decoder. See [Video and GIFs](#video-and-gifs).
- **Download progress.** Every download shows an indeterminate spinner. `File` exposes no
  byte count or fraction; `updateFile` carries one, so this is a small change rather than
  a missing capability.
- **Forwarding.** Needs a chat-picker page plus `forwardMessages`.
- **Message search.** Chat-list search matches **titles only**. Searching message text
  means a `searchMessages` call and a second model.
- **Location, contacts, polls.** The content classes exist and the chat-list preview names
  them, but there is no delegate — they render as "not supported".
- **Playing audio messages in-app.** `MessageAudio` renders through the document bubble:
  it downloads and saves, and opening it is the platform's job. Only *voice* notes play
  here.
- **Video notes** (the round ones). `MessageVideoNote` is a bare class with no properties.
- **Drafts, chat creation, reactions, pinned messages.**
- **Sending your own typing action.**
- **Skin-tone emoji variants** in the picker, and a recents tab.

---

## Performance work

All of it is implemented, and as of 2026-07-31 **part of it is measured** — see
[Profiling](/profiling). The short version: the C++ model layer costs 43.8 ms across a
full-length chat-list flick, and avatar decoding in the same window costs **7.6 seconds**.
The caching work below is confirmed to be as close to free as the instrument can measure
(5.9 µs per read, at its ~5 µs noise floor), and equally confirmed to be ~1% of what
scrolling actually costs. The message-list half is still unmeasured: it is blocked on a
history-paging failure and a segfault.

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

`-DMEEGRAM_GL_VIEWPORT=OFF` remains a half-tested one-flag A/B. Qt 4's `QGraphicsView`
cannot do partial updates with a GL viewport, so on a mostly-static chat list it may
well be slower than software paint. What is now known is the memory side: the SGX GL
context costs **9.1 MiB** of resident set ([Profiling](/profiling#s0-startup-markers)).
Whether it earns that back in frames is still unchecked.
