#include "MessageRow.hpp"

#include "File.hpp"
#include "LottieAnimation.hpp"
#include "MessageContent.hpp"
#include "MessageModel.hpp"
#include "ScopeTimer.hpp"
#include "Utils.hpp"
#include "VoiceNote.hpp"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

using namespace rows;

namespace {

enum Region { None, Bubble, Avatar, Reply, Link, PillBase = 100, CellBase = 200 };

// fontello, from Icons.qml.
const QChar Check1(0xe800);
const QChar Check2(0xe801);
const QChar CloseGlyph(0xe819);
const QChar DocumentGlyph(0xe81f);
const QChar DownloadGlyph(0xe821);
const QChar LargePlayGlyph(0xe82f);
const QChar PauseGlyph(0xe83d);
const QChar PlayGlyph(0xe843);
const QChar SendingGlyph(0xe84c);
const QChar SendingErrorGlyph(0xe84d);

// The body text size every bubble uses.
constexpr int BodySize = 23;

// How far a swipe travels, and how far it has to go to mean "reply" rather than a flick
// that wandered.
constexpr qreal SwipeMax = 90;
constexpr qreal SwipeCommit = 56;

QColor rgba(QRgb value)
{
    return QColor::fromRgba(value);
}

// A wash for something sitting on a message - a reaction pill, a quote. appWindow.tintOn.
QColor tintOn(bool onBubble, bool inverted)
{
    return rgba(onBubble ? 0x40ffffff : inverted ? 0x20ffffff : 0x20000000);
}

qreal lineHeight(const QFont &font)
{
    return QFontMetricsF(font).height();
}

// Photos sent as one batch, in rows of at most three, never leaving one photo alone on the
// last line. Telegram weighs aspect ratios too; this balances the count, the part that
// shows.
std::vector<int> albumRows(int count)
{
    std::vector<int> rows;

    while (count > 0)
    {
        const int take = count <= 3 ? count : (count == 4 || count % 3 == 1 ? 2 : 3);
        rows.push_back(take);
        count -= take;
    }

    return rows;
}

const QPixmap &resourcePixmap(const QString &path)
{
    static QHash<QString, QPixmap> cache;

    auto it = cache.find(path);
    if (it == cache.end())
        it = cache.insert(path, QPixmap(path));

    return it.value();
}

// A reaction's emoji at the pill's 16px, scaled once rather than on every paint.
const QPixmap &emojiIcon(const QString &file)
{
    static QHash<QString, QPixmap> cache;

    auto it = cache.find(file);
    if (it == cache.end())
    {
        const QImage image(QLatin1String(":/emoji/") + file);
        it = cache.insert(file, QPixmap::fromImage(image.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    }

    return it.value();
}

// The fields MessageVideo and MessageAnimation share, by name - a GIF is a soundless mp4
// and the two bubbles differ only by the word on the badge.
template <typename Video>
void readVideo(const Video *video, int &width, int &height, int &duration, QString &caption, QString &fileName, QString &thumbnailFormat, File *&file,
               File *&thumbnail)
{
    width = video->width();
    height = video->height();
    duration = video->duration();
    caption = video->caption();
    fileName = video->fileName();
    thumbnailFormat = video->thumbnailFormat();
    file = video->file();
    thumbnail = video->thumbnailFile();
}

}  // namespace

MessageRow::MessageRow(QDeclarativeItem *parent)
    : ListRow(parent)
    , m_spring(new QPropertyAnimation(this, "x", this))
{
    // Owned by the row rather than shared by the list: a shared animation held its
    // target as a plain pointer, and a quote tap rebuilds the rows - so it went on writing
    // x into a freed delegate. One that dies with its row cannot.
    m_spring->setEndValue(QVariant::fromValue<qreal>(0));
    m_spring->setDuration(150);
    m_spring->setEasingCurve(QEasingCurve::OutQuad);
}

bool MessageRow::bubbles() const noexcept
{
    return m_bubbles;
}

void MessageRow::setBubbles(bool bubbles)
{
    if (bubbles == m_bubbles)
        return;

    m_bubbles = bubbles;
    reloadIfComplete();
}

bool MessageRow::skeuomorphic() const noexcept
{
    return m_skeuomorphic;
}

void MessageRow::setSkeuomorphic(bool skeuomorphic)
{
    if (skeuomorphic == m_skeuomorphic)
        return;

    m_skeuomorphic = skeuomorphic;
    reloadIfComplete();
}

bool MessageRow::animateStickers() const noexcept
{
    return m_animateStickers;
}

void MessageRow::setAnimateStickers(bool animate)
{
    if (animate == m_animateStickers)
        return;

    m_animateStickers = animate;
    reloadIfComplete();
}

QObject *MessageRow::player() const noexcept
{
    return m_player;
}

void MessageRow::setPlayer(QObject *player)
{
    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);

    m_player = qobject_cast<VoiceNote *>(player);

    // The play / pause glyph is the only thing reading it, so a repaint is all it takes.
    if (m_player)
        connect(m_player, SIGNAL(playingChanged()), this, SLOT(playerChanged()));
}

QString MessageRow::flashMessageId() const
{
    return m_flashMessageId;
}

void MessageRow::setFlashMessageId(const QString &messageId)
{
    const bool was = flashing();

    m_flashMessageId = messageId;

    if (was != flashing())
        update();
}

double MessageRow::flashOpacity() const noexcept
{
    return m_flashOpacity;
}

void MessageRow::setFlashOpacity(double opacity)
{
    m_flashOpacity = opacity;

    // Every row hears every step of the animation; only the flashed one paints it.
    if (flashing())
    {
        if (m_lottie)
            m_lottie->setOpacity(opacity);

        update();
    }
}

bool MessageRow::flashing() const
{
    return !m_flashMessageId.isEmpty() && m_flashMessageId == m_idString;
}

qlonglong MessageRow::keyAt(int row) const
{
    return m_model ? m_model->data(m_model->index(row, 0), MessageModel::IdRole).toLongLong() : 0;
}

void MessageRow::reload()
{
    MEEGRAM_SCOPE("MessageRow::reload");

    if (!qobject_cast<MessageModel *>(m_model) || width() <= 0)
        return;

    unwatchFiles();

    m_file = nullptr;
    m_original = nullptr;
    m_thumbnail = nullptr;
    m_fileName.clear();
    m_album = QVariant();
    m_text.clear();
    m_unplayed = false;
    m_spinning = false;
    m_pills.clear();
    m_body.clear();
    m_caption.clear();
    m_badgeText.clear();
    m_nameText.clear();
    m_subText.clear();

    m_idString = data(MessageModel::IdStringRole).toString();
    m_outgoing = data(MessageModel::IsOutgoingRole).toBool();
    m_sender = data(MessageModel::SenderRole).toString();
    m_contentType = data(MessageModel::ContentTypeRole).toString();

    // Drawn by the first message of its batch, so this row draws nothing and takes no room.
    if (m_contentType == QLatin1String("messageAlbumChild"))
    {
        m_kind = Kind::Hidden;
        m_cells.clear();
        m_media.clear();
        dropLottie();
        setImplicitHeight(0);
        setSpinning(false);
        update();
        return;
    }

    if (data(MessageModel::IsServiceRole).toBool())
    {
        layoutService();
        return;
    }

    const qreal rowWidth = width();
    const bool sided = m_outgoing;
    const bool showsSender = data(MessageModel::ShowsSenderRole).toBool();

    m_opensRun = data(MessageModel::OpensRunRole).toBool();

    // The avatar answers "several people are talking", which the model decides; the flat
    // layout hangs one on the first message of a run only. A continuation still clears
    // the gutter, or the column would come apart.
    m_showAvatar = showsSender && (m_bubbles || m_opensRun);
    const qreal avatarSpace = showsSender ? 52 : 0;

    // In the flat layout every run opens with a name, your own messages and 1:1 chats
    // included: with no balloon, the name and the time are what separate messages.
    m_senderVisible = !m_sender.isEmpty() && (m_bubbles || m_opensRun);
    const bool hasTopRow = m_bubbles ? m_senderVisible : true;
    const qreal stackTop = hasTopRow ? 46 : 16;
    const qreal stackLeft = sided ? 80 : 20 + avatarSpace;

    // The time, and the delivery tick before it.
    const QFont dateFont = labelFont(16, false, true);
    const QString date = data(MessageModel::DateRole).toString();
    const qreal dateWidth = QFontMetricsF(dateFont).width(date);

    m_sendState = data(MessageModel::SendStateRole).toString();

    const qreal tickWidth = m_sendState.isEmpty() ? 0 : QFontMetricsF(iconFont(16)).width(Check2) + 6;

    // What the flat layout's top row gives up to the time, so the name stops short of it.
    const qreal timeWidth = m_bubbles ? 0 : dateWidth + tickWidth;

    // The name, and the rank on the far side of it.
    const QFont senderFont = labelFont(20, true);
    const qreal senderBox = m_bubbles ? rowWidth - 100 : rowWidth - stackLeft - 24 - timeWidth;
    const QString sender = Utils::elideEmoji(m_sender, senderFont, static_cast<int>(senderBox));

    m_senderText.set(sender, senderFont, sender != m_sender, senderBox, sided ? Qt::AlignRight : Qt::AlignLeft, false);
    m_senderPos = QPointF(stackLeft, 18);

    // Coloured where it does work: several people talking, or the flat layout, where the
    // name is what separates one run from the next.
    m_coloredSender = m_showAvatar || !m_bubbles;
    m_senderColor = QColor(data(MessageModel::SenderColorRole).toString());

    const QString rank = data(MessageModel::SenderTitleRole).toString();
    m_rankVisible = !rank.isEmpty() && m_senderVisible;
    m_rankText.set(rank, labelFont(18, false, true), false, rowWidth, Qt::AlignLeft, false);

    // The quote, for a reply. Each line takes room only when it has something in it.
    const QFont replySenderFont = labelFont(18, true);
    const QFont replyTextFont = labelFont(18, false, true);
    const QString replySender = data(MessageModel::ReplyToSenderRole).toString();
    const QString replyText = data(MessageModel::ReplyToTextRole).toString().replace(QLatin1Char('\n'), QLatin1Char(' '));

    m_replyToMessageId = data(MessageModel::ReplyToMessageIdRole).toString();
    m_hasReply = !replySender.isEmpty() || !replyText.isEmpty();

    m_replySenderText.set(replySender, replySenderFont, false, rowWidth - 100 - 11, Qt::AlignLeft, false);
    m_replyText.set(replyText, replyTextFont, false, rowWidth - 100 - 11, Qt::AlignLeft, false);

    const qreal replySenderHeight = replySender.isEmpty() ? 0 : lineHeight(replySenderFont);
    const qreal replyHeight = m_hasReply ? replySenderHeight + (replyText.isEmpty() ? 0 : lineHeight(replyTextFont)) : 0;

    const qreal contentTop = stackTop + (m_hasReply ? replyHeight + 6 : 0);

    // The content itself.
    m_childrenWidth = 0;
    const qreal contentHeight = layoutContent(data(MessageModel::ContentRole), contentTop, avatarSpace);
    const qreal contentBottom = contentTop + contentHeight;

    // Reaction pills, between the content and the date. Yours is filled.
    const QFont pillFont = labelFont(16);
    const QFontMetricsF pillMetrics(pillFont);
    qreal pillsWidth = 0;

    for (const auto &value : data(MessageModel::ReactionsRole).toList())
    {
        const auto map = value.toMap();

        Pill pill;
        pill.emoji = map.value(QLatin1String("emoji")).toString();
        pill.icon = map.value(QLatin1String("icon")).toString();
        pill.count = map.value(QLatin1String("count")).toString();
        pill.chosen = map.value(QLatin1String("chosen")).toBool();

        // The emoji image when this build ships one, the character itself when not.
        const qreal glyphWidth = pill.icon.isEmpty() ? pillMetrics.width(pill.emoji) : 16;
        const qreal pillWidth = glyphWidth + 3 + pillMetrics.width(pill.count) + 16;

        pill.rect = QRectF(pillsWidth, 0, pillWidth, 24);
        pillsWidth += pillWidth + 6;

        m_pills.push_back(pill);
    }

    if (!m_pills.empty())
        pillsWidth -= 6;

    // ponytail: one line of pills. Six or so fit in portrait; a Flow is the fix if a chat
    // ever carries more distinct reactions than that.
    const qreal reactionsHeight = m_pills.empty() ? 0 : 30;
    const qreal pillsLeft = sided ? rowWidth - pillsWidth - 20 : 20 + avatarSpace;

    for (auto &pill : m_pills)
        pill.rect.translate(pillsLeft, contentBottom + 6);

    // The date: on the bottom line in a bubble, at the right end of the top row in the
    // flat layout, where the timestamps line up as a column.
    const qreal dateHeight = lineHeight(dateFont);

    if (m_bubbles)
        m_dateBox = QRectF(stackLeft, contentBottom + reactionsHeight + 4, rowWidth - 100, dateHeight);
    else
        m_dateBox = QRectF(stackLeft, 22, rowWidth - stackLeft - 16, dateHeight);

    m_dateText.set(date, dateFont, false, m_dateBox.width(), sided || !m_bubbles ? Qt::AlignRight : Qt::AlignLeft, false);

    // Before the time, not after it: "done, at 13:06", the order Telegram writes it in.
    m_tickRight = m_dateBox.right() - m_dateText.paintedWidth() - 6;

    // Each term only when its part is there, so a plain message keeps the height it had
    // before replies and reactions existed.
    const qreal totalHeight = contentHeight + (m_bubbles ? dateHeight : 0) + (hasTopRow ? lineHeight(senderFont) : 0) + (m_hasReply ? replyHeight + 6 : 0) +
                              reactionsHeight + (sided ? 30 : 28);

    // The balloon wraps whichever is widest: the content, the date line, the name and
    // rank, either quote line, the pills.
    const qreal balloonWidth = qMax(qMax(qMax(m_childrenWidth, dateWidth + tickWidth + (sided ? 0 : 28)),
                                         qMax(m_senderText.paintedWidth() + (m_rankVisible ? m_rankText.paintedWidth() + 16 : 0),
                                              m_hasReply ? qMax(m_replySenderText.paintedWidth(), m_replyText.paintedWidth()) + 11 : 0)),
                                    pillsWidth) +
                               26;

    m_balloon = QRectF(sided ? rowWidth - balloonWidth - 10 : 10 + avatarSpace, 11, balloonWidth, totalHeight - 13);

    // The rank hangs off the balloon's right edge, clear of the time in the flat layout.
    m_rankPos = QPointF(m_balloon.right() - (m_bubbles ? 13 : timeWidth + 8) - m_rankText.paintedWidth(),
                        m_senderPos.y() + m_senderText.ascent() - m_rankText.ascent());

    // In a bubble the avatar hangs off the balloon's bottom edge, where the tail was - and
    // the skeuomorphic asset's tail hangs lower than the rectangle. The flat layout opens
    // with the name, so it sits at the top there.
    const qreal balloonBottom = m_balloon.top() + (m_skeuomorphic ? (sided ? -10 : -3) + m_balloon.height() + 13 + (sided ? 0 : 2) : m_balloon.height());
    m_avatarRect = QRectF(6, m_bubbles ? qMax<qreal>(0, balloonBottom - 40 - 2) : 10, 40, 40);

    m_senderPhoto = m_showAvatar ? data(MessageModel::SenderPhotoRole).value<File *>() : nullptr;

    if (m_senderPhoto && m_senderPhoto->isDownloadingCompleted())
        m_avatar.load(ImageLoader::Avatar, m_senderPhoto->localPath(), QSize(40, 40));
    else
        m_avatar.clear();

    watch(m_senderPhoto);
    downloadOnce(m_senderPhoto);

    // The quote's bar hangs off the balloon's own left edge, on both sides. Its wash is
    // as wide as the text; the block is wider, and that extra width is the tap target.
    m_replyRect = QRectF(m_balloon.left() + 10, stackTop, rowWidth - 100, replyHeight);
    m_replyWashWidth = qMax(m_replySenderText.paintedWidth(), m_replyText.paintedWidth()) + 11 + 8;

    setImplicitHeight(totalHeight);
    setSpinning(m_spinning);
    update();
}

qreal MessageRow::layoutContent(const QVariant &content, qreal top, qreal avatarSpace)
{
    const qreal rowWidth = width();
    const qreal contentWidth = rowWidth - 100;
    const bool sided = m_outgoing;
    const Qt::Alignment align = sided ? Qt::AlignRight : Qt::AlignLeft;

    // Fixed-width content cannot lean on right alignment the way text does, so its
    // outgoing offset is computed - 20 puts its right edge where the text's lands.
    const auto contentLeft = [&](qreal itemWidth) { return avatarSpace + (sided ? rowWidth - itemWidth - 20 : 20); };

    if (m_contentType != QLatin1String("messageSticker"))
        dropLottie();

    if (m_contentType != QLatin1String("messageAlbum"))
        m_cells.clear();

    // TDLib sends a lone emoji as messageAnimatedEmoji, with the same text as messageText.
    if (m_contentType == QLatin1String("messageText") || m_contentType == QLatin1String("messageAnimatedEmoji"))
    {
        m_kind = Kind::Text;
        m_media.clear();

        QString text;
        QString formatted;

        if (const auto *message = content.value<MessageText *>())
        {
            text = message->text();
            formatted = message->formattedText();
        }
        else if (const auto *emoji = content.value<MessageAnimatedEmoji *>())
        {
            text = emoji->text();
            formatted = emoji->formattedText();
        }

        m_text = text;

        // Nothing but one to three emoji draws them large, the way Telegram does.
        const QString html = Utils::replaceEmojiSized(formatted, Utils::emojiOnlySize(text));

        m_body.set(html, labelFont(BodySize), needsRichText(html), contentWidth, align);
        m_bodyPos = QPointF(avatarSpace + (sided ? 80 : 20), top);
        m_childrenWidth = m_body.paintedWidth();

        return m_body.height();
    }

    if (m_contentType == QLatin1String("messagePhoto"))
    {
        m_kind = Kind::Photo;

        const auto *photo = content.value<MessagePhoto *>();
        if (!photo)
            return 0;

        // Never upscale past the source width; the frame's height comes from the metadata
        // so the bubble does not resize under the user when the download lands.
        const qreal photoWidth = qMin(photo->width() > 0 ? qreal(photo->width()) : contentWidth, contentWidth);
        const qreal frameHeight = photo->width() > 0 ? photoWidth * photo->height() / photo->width() : photoWidth;

        m_frame = QRectF(contentLeft(photoWidth), top, photoWidth, frameHeight);
        m_file = photo->file();
        // The original, not the screen-sized copy, is what the menu's Save keeps.
        m_original = photo->originalFile();
        m_text = photo->caption();

        watch(m_file);

        if (isFileReady())
            m_media.load(ImageLoader::Fit, m_file->localPath(), m_frame.size().toSize());
        else
            m_media.clear();

        // ponytail: downloads on sight, ignoring whether the connection is metered. Wire
        // to Telegram's auto-download settings if that bites.
        downloadOnce(m_file);

        m_spinning = !isFileReady();
        m_childrenWidth = photoWidth;

        return frameHeight + layoutCaption(m_text, m_frame.left(), m_frame.bottom() + 6, photoWidth);
    }

    if (m_contentType == QLatin1String("messageAlbum"))
    {
        m_kind = Kind::Album;
        m_media.clear();

        // The whole batch hangs off its first row; the caption is on whichever member of
        // it carries one, which the model finds.
        m_album = data(MessageModel::AlbumRole);
        m_text = data(MessageModel::AlbumCaptionRole).toString();

        if (const auto *head = content.value<MessagePhoto *>())
            m_original = head->originalFile();

        const auto photos = m_album.toList();
        const qreal left = contentLeft(contentWidth);

        std::vector<Cell> cells(photos.size());
        int index = 0;
        qreal y = top;

        for (const int columns : albumRows(photos.size()))
        {
            const int cellSize = static_cast<int>((contentWidth - 4 * (columns - 1)) / columns);

            for (int column = 0; column < columns; ++column, ++index)
            {
                auto &cell = cells[index];
                const auto *photo = photos.at(index).value<MessagePhoto *>();

                cell.rect = QRectF(left + column * (cellSize + 4), y, cellSize, cellSize);

                // Kept when the row is re-read for the same batch, so a reaction arriving
                // does not flash every cell back to its scrim.
                if (index < static_cast<int>(m_cells.size()))
                    cell.image = m_cells[index].image;

                if (!photo)
                    continue;

                cell.file = photo->file();
                cell.original = photo->originalFile();

                watch(cell.file);

                if (cell.file && cell.file->isDownloadingCompleted())
                    cell.image.load(ImageLoader::Crop, cell.file->localPath(), QSize(cellSize, cellSize));
                else
                    cell.image.clear();

                downloadOnce(cell.file);
            }

            y += cellSize + 6;
        }

        m_cells.swap(cells);
        m_childrenWidth = contentWidth;

        const qreal mosaicHeight = photos.isEmpty() ? 0 : y - 6 - top;

        return mosaicHeight + layoutCaption(m_text, left, top + mosaicHeight + 6, contentWidth);
    }

    if (m_contentType == QLatin1String("messageSticker"))
    {
        m_kind = Kind::Sticker;

        const auto *sticker = content.value<MessageSticker *>();
        if (!sticker)
            return 0;

        // 512px stickers drawn at 512 would swallow the screen; Telegram sizes them well
        // under a photo.
        const qreal stickerWidth = qMin(sticker->width() > 0 ? qreal(sticker->width()) : 180.0, 180.0);
        const qreal stickerHeight = sticker->width() > 0 && sticker->height() > 0 ? stickerWidth * sticker->height() / sticker->width() : stickerWidth;

        m_frame = QRectF(contentLeft(stickerWidth), top, stickerWidth, stickerHeight);
        m_file = sticker->file();

        watch(m_file);

        const QString format = sticker->format();
        const bool downloaded = isFileReady();

        // tgs through rlottie, webp through libwebp. webm has no decoder here and falls
        // back to the emoji, and so does a tgs with "Animated stickers" off.
        const bool animated = downloaded && format == QLatin1String("tgs") && m_animateStickers;
        const bool still = downloaded && format == QLatin1String("webp");

        if (animated)
            showLottie(m_file->localPath());
        else
            dropLottie();

        if (still)
            m_media.load(ImageLoader::Webp, m_file->localPath(), m_frame.size().toSize());
        else
            m_media.clear();

        if (!animated && !still)
        {
            m_body.set(Utils::replaceEmoji(sticker->emoji()) + QLatin1Char(' ') + tr("AttachSticker"), labelFont(BodySize), true, stickerWidth,
                       Qt::AlignHCenter);
            m_bodyPos = QPointF(m_frame.left(), m_frame.center().y() - m_body.height() / 2);
        }

        // Only fetch what can be drawn: a webm would be pure cost, and a tgs nobody is
        // going to play waits until the setting comes back on.
        if (format == QLatin1String("webp") || (format == QLatin1String("tgs") && m_animateStickers))
            downloadOnce(m_file);

        m_childrenWidth = stickerWidth;

        return stickerHeight;
    }

    // Video and GIF both: the same bubble with a different word on the badge.
    if (m_contentType == QLatin1String("messageVideo") || m_contentType == QLatin1String("messageAnimation"))
    {
        m_kind = Kind::Video;

        int videoWidth = 0;
        int videoHeight = 0;
        int duration = 0;
        QString thumbnailFormat;
        File *file = nullptr;
        File *thumbnail = nullptr;

        if (const auto *video = content.value<MessageVideo *>())
            readVideo(video, videoWidth, videoHeight, duration, m_text, m_fileName, thumbnailFormat, file, thumbnail);
        else if (const auto *animation = content.value<MessageAnimation *>())
            readVideo(animation, videoWidth, videoHeight, duration, m_text, m_fileName, thumbnailFormat, file, thumbnail);

        const qreal frameWidth = qMin(videoWidth > 0 ? qreal(videoWidth) : contentWidth, contentWidth);
        // 4:3 for a video that reports no dimensions.
        const qreal frameHeight = videoWidth > 0 && videoHeight > 0 ? frameWidth * videoHeight / videoWidth : frameWidth * 3 / 4;

        m_frame = QRectF(contentLeft(frameWidth), top, frameWidth, frameHeight);
        m_file = file;
        m_thumbnail = thumbnail;

        watch(m_file);
        watch(m_thumbnail);

        // Only the still is fetched on sight - a few KB, and without it the bubble is a
        // grey box. The video waits for a tap.
        if (m_thumbnail && m_thumbnail->isDownloadingCompleted())
            m_media.load(thumbnailFormat == QLatin1String("webp") ? ImageLoader::Webp : ImageLoader::Fit, m_thumbnail->localPath(), m_frame.size().toSize());
        else
            m_media.clear();

        downloadOnce(m_thumbnail);

        // Off the message rather than the file, so the badge reads right before anything
        // has downloaded. GIF in place of the runtime, which is how Telegram marks one.
        const QString runtime = m_contentType == QLatin1String("messageAnimation") ? QString::fromLatin1("GIF") : Utils::formatTime(duration);
        const QString size = m_file ? m_file->size() : QString();
        const QString badge = !runtime.isEmpty() && !size.isEmpty() ? runtime + QLatin1String(" - ") + size : runtime + size;

        m_badgeText.set(badge, labelFont(18, false, true), false, frameWidth, Qt::AlignLeft, false);
        m_badgeRect = QRectF(m_frame.left() + 6, m_frame.top() + 6, m_badgeText.paintedWidth() + 12, m_badgeText.height() + 6);

        m_spinning = isFileActive();
        m_childrenWidth = frameWidth;

        return frameHeight + layoutCaption(m_text, m_frame.left(), m_frame.bottom() + 6, frameWidth);
    }

    // Documents and audio files: a file row under the sender's filename. Playing an mp3 is
    // the platform's job, so audio goes the same way a document does.
    if (m_contentType == QLatin1String("messageDocument") || m_contentType == QLatin1String("messageAudio"))
    {
        m_kind = Kind::Document;
        m_media.clear();

        if (const auto *document = content.value<MessageDocument *>())
        {
            m_file = document->file();
            m_fileName = document->fileName();
            m_text = document->caption();
        }
        else if (const auto *audio = content.value<MessageAudio *>())
        {
            m_file = audio->file();
            m_fileName = audio->fileName();
            m_text = audio->caption();
        }

        // Deliberately not fetched on sight: a document has no size ceiling and this is a
        // metered radio.
        watch(m_file);

        m_spinning = isFileActive();
        m_childrenWidth = contentWidth;

        const qreal left = contentLeft(contentWidth);
        const qreal rowHeight =
            layoutFileRow(left, top, contentWidth, m_fileName.isEmpty() ? tr("AttachDocument") : m_fileName, fileSubtitle());

        return rowHeight + layoutCaption(m_text, left, top + rowHeight + 6, contentWidth);
    }

    if (m_contentType == QLatin1String("messageVoiceNote"))
    {
        m_kind = Kind::Voice;
        m_media.clear();

        int duration = 0;

        if (const auto *voice = content.value<MessageVoiceNote *>())
        {
            m_file = voice->file();
            m_text = voice->caption();
            duration = voice->duration();

            // Incoming only - a note you sent is listened by definition.
            m_unplayed = !m_outgoing && !voice->isListened();
        }

        watch(m_file);

        // The one file fetched on sight rather than on tap: a voice note is a few KB, and
        // the point of one is a single press of play. Unless the user stopped it - then it
        // stays stopped for as long as the chat is open.
        if (m_file && !act("isDownloadCancelled", QVariantList() << m_file->id()).toBool())
            downloadOnce(m_file);

        m_spinning = isFileActive();
        m_childrenWidth = contentWidth;

        const qreal left = contentLeft(contentWidth);
        const qreal rowHeight = layoutFileRow(left, top, contentWidth, tr("AttachAudio"), Utils::formatTime(duration));

        // The unplayed dot, after the duration.
        m_dotRect = QRectF(m_subPos.x() + m_subText.paintedWidth() + 8, m_subPos.y() + (m_subText.height() - 12) / 2, 12, 12);

        return rowHeight + layoutCaption(m_text, left, top + rowHeight + 6, contentWidth);
    }

    m_kind = Kind::Unsupported;
    m_media.clear();

    m_body.set(tr("UnsupportedAttachment"), labelFont(BodySize, true), false, contentWidth, align);
    m_bodyPos = QPointF(avatarSpace + (sided ? 80 : 20), top);
    m_childrenWidth = m_body.paintedWidth();

    return m_body.height();
}

qreal MessageRow::layoutCaption(const QString &caption, qreal x, qreal y, qreal width)
{
    const QString html = Utils::replaceEmoji(caption);

    m_caption.set(html, labelFont(BodySize), needsRichText(html), width, m_outgoing ? Qt::AlignRight : Qt::AlignLeft);
    m_captionPos = QPointF(x, y);

    return m_caption.isEmpty() ? 0 : 6 + m_caption.height();
}

QString MessageRow::fileSubtitle() const
{
    if (!m_file)
        return QString();

    // "1.4 MB / 6.2 MB" while it arrives, the total on its own otherwise. The ring says
    // how far along it is; this says how big the wait actually is, which is the number
    // worth having on a metered connection.
    if (m_file->isDownloadingActive() && !m_file->downloadedSize().isEmpty())
        return m_file->downloadedSize() + QLatin1String(" / ") + m_file->size();

    return m_file->size();
}

qreal MessageRow::layoutFileRow(qreal x, qreal top, qreal width, const QString &title, const QString &subtitle)
{
    m_iconRect = QRectF(x, top, 60, 60);

    const qreal columnLeft = x + 60 + 12;
    const qreal columnWidth = width - 60 - 12;

    // ElideMiddle: the extension is the part that says what the file is.
    m_nameText.set(title, labelFont(BodySize), false, columnWidth, Qt::AlignLeft, false, Qt::ElideMiddle);
    m_subText.set(subtitle, labelFont(18, false, true), false, columnWidth, Qt::AlignLeft, false);

    m_namePos = QPointF(columnLeft, top);
    m_subPos = QPointF(columnLeft, top + m_nameText.height() + 2);

    const qreal column = m_nameText.height() + (m_subText.isEmpty() ? 0 : 2 + m_subText.height());

    return qMax<qreal>(60, column);
}

void MessageRow::layoutService()
{
    m_kind = Kind::Service;
    m_media.clear();
    m_cells.clear();
    m_showAvatar = false;
    m_hasReply = false;
    m_text.clear();
    dropLottie();

    // ServiceMessageDelegate: a centred label in the notification balloon, both padded by
    // the platform's margins.
    const qreal rowWidth = width() - 2 * 16;
    const qreal labelWidth = rowWidth - 2 * 16;
    const QString text = Utils::replaceEmoji(data(MessageModel::ServiceMessageRole).toString());

    m_body.set(text, labelFont(18), Qt::mightBeRichText(text), labelWidth, Qt::AlignHCenter);

    const qreal boxWidth = qMin(m_body.paintedWidth() + 2 * 16, rowWidth - 2 * 16);
    const qreal boxHeight = m_body.height() + 4 * 8;

    m_balloon = QRectF(16 + (rowWidth - boxWidth) / 2, 0, boxWidth, boxHeight);
    m_bodyPos = QPointF(m_balloon.center().x() - labelWidth / 2, (boxHeight - m_body.height()) / 2);

    setImplicitHeight(boxHeight);
    setSpinning(false);
    update();
}

void MessageRow::showLottie(const QString &path)
{
    // Rebuilt only for a different sticker: a row re-read for its reactions must not play
    // the animation over again.
    if (!m_lottie || m_lottiePath != path)
    {
        dropLottie();

        m_lottie = new LottieAnimation(this);
        m_lottiePath = path;

        m_lottie->setWidth(m_frame.width());
        m_lottie->setHeight(m_frame.height());
        // Telegram plays a sticker on arrival rather than looping it forever.
        m_lottie->setLoop(1);

        connect(m_lottie, SIGNAL(statusChanged()), this, SLOT(startLottie()));

        m_lottie->setSource(QUrl::fromLocalFile(path));
    }

    m_lottie->setPos(m_frame.topLeft());
    m_lottie->setWidth(m_frame.width());
    m_lottie->setHeight(m_frame.height());
}

void MessageRow::dropLottie()
{
    delete m_lottie;
    m_lottie = nullptr;
    m_lottiePath.clear();
}

void MessageRow::playerChanged()
{
    update();
}

void MessageRow::startLottie()
{
    if (m_lottie && m_lottie->status() == LottieAnimation::Ready)
        m_lottie->play();
}

bool MessageRow::isFileActive() const
{
    return m_file && m_file->isDownloadingActive();
}

bool MessageRow::isFileReady() const
{
    return m_file && m_file->isDownloadingCompleted();
}

QColor MessageRow::contentColor() const
{
    // White reads on the accent balloon and on nothing else.
    return m_outgoing && m_bubbles ? QColor(Qt::white) : Palette::get(m_inverted).bubbleText;
}

int MessageRow::hitTest(const QPointF &point) const
{
    if (m_kind == Kind::Hidden || m_kind == Kind::Service)
        return None;

    // Top-most first, the order the QML items were stacked in.
    for (size_t i = 0; i < m_pills.size(); ++i)
    {
        if (m_pills[i].rect.contains(point))
            return PillBase + static_cast<int>(i);
    }

    for (size_t i = 0; i < m_cells.size(); ++i)
    {
        if (m_cells[i].rect.contains(point))
            return CellBase + static_cast<int>(i);
    }

    if ((m_kind == Kind::Text && !m_body.anchorAt(point - m_bodyPos).isEmpty()) || !m_caption.anchorAt(point - m_captionPos).isEmpty())
        return Link;

    // A quote pointing nowhere takes no tap, and the balloon under it gets it instead.
    if (m_hasReply && !m_replyToMessageId.isEmpty() && m_replyRect.contains(point))
        return Reply;

    if (m_showAvatar && m_avatarRect.contains(point))
        return Avatar;

    // The balloon is this message's input surface in the flat layout too, where it is not
    // drawn.
    if (m_balloon.contains(point))
        return Bubble;

    return None;
}

void MessageRow::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    ListRow::mousePressEvent(event);

    if (!event->isAccepted())
        return;

    m_spring->stop();
    m_swipeStartX = x();
    m_pressPoint = listPoint(event);
    m_swipeRefused = false;
}

QPointF MessageRow::listPoint(const QGraphicsSceneMouseEvent *event) const
{
    return parentItem() ? parentItem()->mapFromScene(event->scenePos()) : event->scenePos();
}

void MessageRow::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    // Swipe left to reply, from anything but a link or the avatar. Telegram drags the
    // other way; this does not, deliberately.
    //
    // The grab is taken the way MouseArea took it for an x-axis drag: past the threshold
    // sideways while still inside it vertically. Without the vertical half, any scroll
    // whose finger drifted sideways first became a swipe and the list stopped moving for
    // the rest of the gesture. Stricter than MouseArea in two ways: once the finger has
    // gone vertical the press stays a scroll, and only a leftward drag counts - the row
    // cannot move right, so grabbing one only ever cost a scroll.
    const bool swipeable = m_pressRegion != Link && m_pressRegion != Avatar && m_pressRegion != None;
    const QPointF delta = listPoint(event) - m_pressPoint;
    const qreal dx = delta.x();
    const int threshold = QApplication::startDragDistance();

    if (swipeable && !m_dragging && !m_swipeRefused)
    {
        if (qAbs(delta.y()) > threshold)
        {
            m_swipeRefused = true;
        }
        else if (dx < -threshold)
        {
            m_dragging = true;
            stopHold();
            setKeepMouseGrab(true);
        }
    }

    if (m_dragging)
        setX(qBound<qreal>(-SwipeMax, m_swipeStartX + dx, 0));

    ListRow::mouseMoveEvent(event);
}

void MessageRow::pressEnded(bool cancelled)
{
    setKeepMouseGrab(false);

    // A cancelled gesture is not a reply - the list took the grab mid-drag.
    if (m_dragging && !cancelled && x() <= -SwipeCommit)
    {
        // Each content type keeps its wording under a different name; an album's is on
        // another message of the batch, which the model already found.
        act("reply", QVariantList() << m_idString << m_sender << m_text);
    }

    // Only if the row actually moved: a tap would otherwise animate x from 0 to 0.
    if (x() != 0)
        m_spring->start();
}

void MessageRow::clicked(int region, const QPointF &point)
{
    auto *model = qobject_cast<MessageModel *>(m_model);
    if (!model)
        return;

    // The row may have been re-read under the finger; a region that is gone does nothing.
    if (region >= CellBase)
    {
        if (region - CellBase >= static_cast<int>(m_cells.size()))
            return;

        const auto &cell = m_cells[region - CellBase];

        // With this cell's own original, so the viewer's Save keeps the full size.
        if (cell.file && cell.file->isDownloadingCompleted())
            act("openPhoto", QVariantList() << cell.file->localPath() << QVariant::fromValue<QObject *>(cell.original.data()));

        return;
    }

    if (region >= PillBase)
    {
        if (region - PillBase >= static_cast<int>(m_pills.size()))
            return;

        // Nothing is drawn optimistically: TDLib answers with the new counts and that is
        // what repaints the row.
        model->toggleReaction(m_idString, m_pills[region - PillBase].emoji);
        return;
    }

    switch (region)
    {
        case Avatar: {
            // Empty when a chat rather than a person sent it - a channel signing its posts.
            const QString userId = model->senderUserId(m_idString);

            if (!userId.isEmpty())
                act("openProfile", QVariantList() << userId);

            return;
        }
        case Reply:
            act("goToMessage", QVariantList() << m_replyToMessageId);
            return;
        case Link: {
            QString link = m_kind == Kind::Text ? m_body.anchorAt(point - m_bodyPos) : QString();

            if (link.isEmpty())
                link = m_caption.anchorAt(point - m_captionPos);

            if (!link.isEmpty())
                act("openLink", QVariantList() << link);

            return;
        }
        case Bubble:
            break;
        default:
            return;
    }

    switch (m_kind)
    {
        case Kind::Photo:
            // The bubble's own copy fills the viewer at once; the original goes with it and
            // the viewer swaps to it once downloaded.
            if (isFileReady())
                act("openPhoto", QVariantList() << m_file->localPath() << QVariant::fromValue<QObject *>(m_original.data()));
            break;
        case Kind::Video:
        case Kind::Document:
            openOrFetch();
            break;
        case Kind::Voice:
            toggleVoice();
            break;
        default:
            break;
    }
}

void MessageRow::openOrFetch()
{
    if (!m_file)
        return;

    if (m_file->isDownloadingCompleted())
    {
        // False means nothing on the device is registered for the type. Saying so beats a
        // tap that looks like it did nothing.
        if (!Utils::openFile(m_file->localPath()))
            act("showBanner", QVariantList() << tr("ErrorOccurred"));
    }
    else if (m_file->isDownloadingActive())
    {
        // The tap that started it stops it - a video started by mistake on a metered radio
        // has to be stoppable without leaving the chat.
        act("cancelDownload", QVariantList() << m_file->id());
    }
    else if (m_file->canBeDownloaded())
    {
        act("download", QVariantList() << m_file->id());
    }
}

void MessageRow::toggleVoice()
{
    if (!m_file)
        return;

    const int fileId = m_file->id();

    // Fetched on sight, so this is the one download nobody asked for - which is why it
    // has to be stoppable, and remembered as stopped.
    if (m_file->isDownloadingActive())
    {
        act("cancelDownload", QVariantList() << fileId);
        act("setDownloadCancelled", QVariantList() << fileId << true);
        return;
    }

    if (!m_file->isDownloadingCompleted())
    {
        if (m_file->canBeDownloaded())
        {
            // Asked for by hand this time, so it is no longer "not now".
            act("setDownloadCancelled", QVariantList() << fileId << false);
            act("download", QVariantList() << fileId);
        }

        return;
    }

    if (!m_player)
        return;

    const QString path = m_file->localPath();

    if (m_player->isPlaying() && m_player->source() == path)
    {
        m_player->stop();
    }
    else
    {
        m_player->play(path);

        // Listened lives on the message content, not on the read pointer - without this
        // the note stays unplayed on the other clients.
        if (auto *model = qobject_cast<MessageModel *>(m_model))
            model->openMessageContent(m_idString);
    }
}

void MessageRow::held(int region, const QPointF &point)
{
    Q_UNUSED(point);

    // Save is per photo in an album: the held cell hands over its own original. The
    // other actions act on the first message of the batch.
    if (region >= CellBase)
    {
        if (region - CellBase < static_cast<int>(m_cells.size()))
            openMenu(m_cells[region - CellBase].original, QString(), m_album);

        return;
    }

    if (region != Bubble)
        return;

    switch (m_kind)
    {
        case Kind::Photo:
            openMenu(m_original);
            break;
        case Kind::Album:
            openMenu(m_original, QString(), m_album);
            break;
        // The file goes with its name: a document is saved under the name its sender
        // gave it, not TDLib's cache name.
        case Kind::Video:
        case Kind::Document:
            openMenu(m_file, m_fileName);
            break;
        case Kind::Voice:
            openMenu(m_file);
            break;
        default:
            // Text, stickers and the unsupported: nothing to save.
            openMenu(nullptr);
            break;
    }
}

void MessageRow::openMenu(File *file, const QString &fileName, const QVariant &photos)
{
    act("openMenu", QVariantList() << m_idString << m_sender << m_text << m_outgoing << QVariant::fromValue<QObject *>(file) << fileName << photos);
}

void MessageRow::imageReady(const QString &key)
{
    bool mine = m_avatar.take(key) || m_media.take(key);

    for (auto &cell : m_cells)
        mine = cell.image.take(key) || mine;

    if (mine)
        update();
}

void MessageRow::paintSpinner(QPainter *painter, const QPointF &center) const
{
    const QPixmap frame = spinnerFrame(this, m_inverted, 32);
    painter->drawPixmap(QPointF(center.x() - frame.width() / 2.0, center.y() - frame.height() / 2.0), frame);
}

void MessageRow::paintProgress(QPainter *painter, const QPointF &center) const
{
    const double progress = m_file ? m_file->progress() : 0.0;

    // Nothing has arrived yet, so there is no fraction to draw and the turning spinner is
    // the only thing that says the request is alive.
    if (progress <= 0.0)
    {
        paintSpinner(painter, center);
        return;
    }

    // The spinner's own footprint, so the ring lands where the glyph expects it and the
    // small cross drawn inside it still reads.
    constexpr qreal Ring = 32;
    constexpr int Thickness = 3;

    const QRectF ring(center.x() - Ring / 2, center.y() - Ring / 2, Ring, Ring);

    const PainterGuard guard(painter);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);

    QPen pen(QColor(255, 255, 255, 0x60));
    pen.setWidth(Thickness);
    painter->setPen(pen);
    painter->drawEllipse(ring);

    pen.setColor(Qt::white);
    painter->setPen(pen);

    // Sixteenths of a degree, from twelve o'clock, clockwise - which is negative in Qt's
    // anticlockwise convention.
    painter->drawArc(ring, 90 * 16, -static_cast<int>(progress * 360 * 16));
}

void MessageRow::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("MessageRow::paint");

    if (m_kind == Kind::Hidden)
        return;

    const PainterGuard guard(painter);
    const auto &palette = Palette::get(m_inverted);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_kind == Kind::Service)
    {
        drawBorderImage(painter, m_balloon, resourcePixmap(QLatin1String(":/images/notification.png")), 22);
        m_body.draw(painter, m_bodyPos, QColor(0x80, 0x80, 0x80));
        return;
    }

    // A hairline between one run and the next in the flat layout, drawn at the top of the
    // message that opens a run - nothing dangles under the newest one.
    if (!m_bubbles && m_row > 0 && m_opensRun)
        painter->fillRect(QRectF(16, 0, width() - 32, 1), QColor(palette.separator.red(), palette.separator.green(), palette.separator.blue(), 0x80));

    // The jump from a quote dips the whole message and brings it back.
    if (flashing())
        painter->setOpacity(painter->opacity() * m_flashOpacity);

    const bool onBubble = m_outgoing && m_bubbles;
    const bool pressed = m_pressRegion == Bubble;

    if (m_bubbles && m_skeuomorphic)
    {
        // The 2012 balloon: a nine-slice whose box is bigger than its ink and sits higher,
        // so the ink lands on the rectangle's own edges and only the tail shows.
        const QString image =
            QString::fromLatin1(":/images/%1-%2%3.png")
                .arg(m_outgoing ? QLatin1String("outgoing") : QLatin1String("incoming"), pressed ? QLatin1String("pressed") : QLatin1String("normal"),
                     m_inverted ? QLatin1String("-inverted") : QLatin1String(""));

        const QRectF box(m_balloon.left(), m_balloon.top() + (m_outgoing ? -10 : -3), m_balloon.width(), m_balloon.height() + 13 + (m_outgoing ? 0 : 2));

        drawBorderImage(painter, box, resourcePixmap(image), 22);
    }
    else if (m_bubbles)
    {
        // Sampled from the balloon assets, in their four states.
        QRgb fill;

        if (m_outgoing)
            fill = pressed ? (m_inverted ? 0xff0e8aa8 : 0xff06657b) : (m_inverted ? 0xff0a6a82 : 0xff0aa7cc);
        else
            fill = pressed ? (m_inverted ? 0xff3d3d3d : 0xff939393) : (m_inverted ? 0xff2b2b2b : 0xfff5f5f5);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(rgba(fill));
        // 13px, the radius the balloon assets were drawn with.
        painter->drawRoundedRect(m_balloon, 13, 13);
        painter->restore();
    }

    if (m_showAvatar)
    {
        if (m_senderPhoto && m_senderPhoto->isDownloadingCompleted())
        {
            if (!m_avatar.image.isNull())
                painter->drawImage(m_avatarRect, m_avatar.image);
        }
        else
        {
            const QPixmap placeholder =
                themePixmap(this, m_inverted ? QLatin1String("icon-m-content-avatar-placeholder-inverse") : QLatin1String("icon-l-content-avatar-placeholder"));
            painter->drawPixmap(m_avatarRect.toRect(), placeholder);
        }
    }

    if (m_senderVisible)
    {
        const QColor senderColor = onBubble ? QColor(Qt::white) : m_coloredSender ? m_senderColor : palette.bubbleText;

        m_senderText.draw(painter, m_senderPos, senderColor);

        if (m_rankVisible)
        {
            const qreal opacity = painter->opacity();
            painter->setOpacity(opacity * 0.6);
            m_rankText.draw(painter, m_rankPos, senderColor);
            painter->setOpacity(opacity);
        }
    }

    if (m_hasReply)
    {
        const qreal opacity = painter->opacity();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tintOn(onBubble, m_inverted));
        painter->drawRoundedRect(QRectF(m_replyRect.left(), m_replyRect.top() - 3, m_replyWashWidth, m_replyRect.height() + 6), 4, 4);
        painter->restore();

        painter->setOpacity(opacity * (onBubble ? 0.6 : 1.0));
        painter->fillRect(QRectF(m_replyRect.left(), m_replyRect.top(), 3, m_replyRect.height()), onBubble ? QColor(Qt::white) : palette.bubbleAccent);
        painter->setOpacity(opacity);

        m_replySenderText.draw(painter, QPointF(m_replyRect.left() + 11, m_replyRect.top()), onBubble ? QColor(Qt::white) : palette.bubbleAccent);

        painter->setOpacity(opacity * (onBubble ? 0.75 : 1.0));
        m_replyText.draw(painter, QPointF(m_replyRect.left() + 11, m_replyRect.top() + (m_replySenderText.isEmpty() ? 0 : m_replySenderText.height())),
                         onBubble ? QColor(Qt::white) : palette.bubbleSecondary);
        painter->setOpacity(opacity);
    }

    paintContent(painter, palette);

    if (!m_pills.empty())
    {
        const QFont font = labelFont(16);
        const QFontMetricsF metrics(font);

        painter->setFont(font);

        for (const auto &pill : m_pills)
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(pill.chosen ? (onBubble ? QColor(Qt::white) : QColor(0x00, 0x77, 0xa8)) : tintOn(onBubble, m_inverted));
            painter->drawRoundedRect(pill.rect, 12, 12);
            painter->restore();

            const qreal glyphWidth = pill.icon.isEmpty() ? metrics.width(pill.emoji) : 16;
            const qreal innerWidth = glyphWidth + 3 + metrics.width(pill.count);
            const qreal left = pill.rect.left() + (pill.rect.width() - innerWidth) / 2;
            const qreal baseline = pill.rect.center().y() + (metrics.ascent() - metrics.descent()) / 2;

            if (pill.icon.isEmpty())
            {
                painter->setPen(pill.chosen && !onBubble ? QColor(Qt::white) : onBubble ? QColor(Qt::black) : palette.bubbleText);
                painter->drawText(QPointF(left, baseline), pill.emoji);
            }
            else
            {
                painter->drawPixmap(QPointF(left, pill.rect.center().y() - 8), emojiIcon(pill.icon));
            }

            painter->setPen(pill.chosen ? (onBubble ? QColor(0x00, 0x77, 0xa8) : QColor(Qt::white)) : (onBubble ? QColor(Qt::white) : palette.bubbleText));
            painter->drawText(QPointF(left + glyphWidth + 3, baseline), pill.count);
        }
    }

    m_dateText.draw(painter, m_dateBox.topLeft(), onBubble ? QColor(Qt::white) : palette.bubbleText);

    // A clock while it is on its way, a tick once the server has it, a second one once it
    // has been read. On a balloon read is the one state with a colour; on the page the
    // accent marks read and the secondary grey the rest, as Telegram's flat mode does.
    if (!m_sendState.isEmpty())
    {
        const bool read = m_sendState == QLatin1String("read");
        const QChar glyph = m_sendState == QLatin1String("sending")  ? SendingGlyph
                            : m_sendState == QLatin1String("failed") ? SendingErrorGlyph
                            : read                                   ? Check2
                                                                     : Check1;
        const QFont font = iconFont(16);
        const qreal opacity = painter->opacity();

        painter->setFont(font);
        painter->setPen(m_bubbles ? (read ? QColor(0x7b, 0xe8, 0x7b) : QColor(Qt::white)) : (read ? palette.accent : palette.bubbleSecondary));
        painter->setOpacity(opacity * (read || !m_bubbles ? 1.0 : 0.75));
        painter->drawText(QPointF(m_tickRight - QFontMetricsF(font).width(glyph), m_dateBox.top() + m_dateText.ascent()), QString(glyph));
        painter->setOpacity(opacity);
    }
}

void MessageRow::paintContent(QPainter *painter, const Palette &palette) const
{
    const bool onBubble = m_outgoing && m_bubbles;
    const QColor text = contentColor();
    // #AARRGGBB light scrim that reads on both bubble colours, behind media not yet in.
    const QColor scrim = rgba(0x30000000);

    switch (m_kind)
    {
        case Kind::Text:
        case Kind::Unsupported:
            m_body.draw(painter, m_bodyPos, text);
            break;

        case Kind::Photo:
            if (!isFileReady())
            {
                painter->fillRect(m_frame, scrim);
                paintProgress(painter, m_frame.center());
            }
            else
            {
                drawFitted(painter, m_frame, m_media.image);
            }
            break;

        case Kind::Album:
            // No spinner per cell: ten of those turning at once is not what the N9 has to
            // spare.
            for (const auto &cell : m_cells)
            {
                if (cell.file && cell.file->isDownloadingCompleted() && !cell.image.image.isNull())
                    drawCropped(painter, cell.rect, cell.image.image);
                else
                    painter->fillRect(cell.rect, scrim);
            }
            break;

        case Kind::Sticker:
            // An animated one is the LottieAnimation child, which paints itself.
            if (!m_media.image.isNull())
                painter->drawImage(m_frame, m_media.image);
            else if (!m_lottie)
                m_body.draw(painter, m_bodyPos, text);
            break;

        case Kind::Video: {
            if (m_thumbnail && m_thumbnail->isDownloadingCompleted())
                drawFitted(painter, m_frame, m_media.image);
            else
                painter->fillRect(m_frame, scrim);

            // Download, then play: two states for two taps, so the first does not look like
            // a play button that did nothing. The spinner replaces the glyph.
            const QRectF button(m_frame.center().x() - 40, m_frame.center().y() - 40, 80, 80);

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(rgba(0x80000000));
            painter->drawEllipse(button);
            painter->restore();

            if (isFileActive())
            {
                paintProgress(painter, button.center());
            }
            else
            {
                painter->setFont(iconFont(40));
                painter->setPen(Qt::white);
                painter->drawText(button, Qt::AlignCenter, QString(isFileReady() ? LargePlayGlyph : DownloadGlyph));
            }

            if (!m_badgeText.isEmpty())
            {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(rgba(0x80000000));
                painter->drawRoundedRect(m_badgeRect, 4, 4);
                painter->restore();

                m_badgeText.draw(painter, QPointF(m_badgeRect.left() + 6, m_badgeRect.top() + 3), Qt::white);
            }
            break;
        }

        case Kind::Document:
        case Kind::Voice: {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(onBubble ? rgba(0x40ffffff) : palette.bubbleAccent);
            painter->drawEllipse(m_iconRect);
            painter->restore();

            // While it is arriving: the spinner, with a small cross inside its ring that
            // says a tap stops it.
            QChar glyph;

            if (isFileActive())
                glyph = CloseGlyph;
            else if (!isFileReady())
                glyph = DownloadGlyph;
            else if (m_kind == Kind::Document)
                glyph = DocumentGlyph;
            else
                glyph = m_player && m_player->isPlaying() && m_player->source() == m_file->localPath() ? PauseGlyph : PlayGlyph;

            painter->setFont(iconFont(isFileActive() ? 18 : 28));
            painter->setPen(Qt::white);
            painter->drawText(m_iconRect, Qt::AlignCenter, QString(glyph));

            if (isFileActive())
                paintProgress(painter, m_iconRect.center());

            m_nameText.draw(painter, m_namePos, text);

            const qreal opacity = painter->opacity();
            painter->setOpacity(opacity * (onBubble ? 0.75 : 1.0));
            m_subText.draw(painter, m_subPos, onBubble ? QColor(Qt::white) : palette.bubbleSecondary);
            painter->setOpacity(opacity);

            if (m_kind == Kind::Voice && m_unplayed)
            {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(palette.bubbleAccent);
                painter->drawEllipse(m_dotRect);
                painter->restore();
            }
            break;
        }

        default:
            break;
    }

    m_caption.draw(painter, m_captionPos, text);
}
