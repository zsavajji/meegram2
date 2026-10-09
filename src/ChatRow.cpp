#include "ChatRow.hpp"

#include "Chat.hpp"
#include "ChatModel.hpp"
#include "File.hpp"
#include "ScopeTimer.hpp"
#include "Utils.hpp"

#include <QPainter>

using namespace rows;

namespace {

// ChatItem.qml's geometry: an 88px row, a 64px avatar 12px in, and two text lines
// starting 16px past it - a 45px title line and a 30px line under it.
constexpr int RowHeight = 88;
constexpr int AvatarSize = 64;
constexpr int TextLeft = 12 + AvatarSize + 16;
constexpr int TitleLineHeight = 45;
constexpr int SecondLineHeight = 30;
constexpr int BubbleSize = 32;

const QString ChannelGlyph = QString(QChar(0xe811));
const QString PinnedGlyph = QString(QChar(0xe842));
const QString MentionGlyph = QString(QChar(0xe85a));

// Count bubbles, as MyCountBubble drew them. theme.colorString is empty: this app never
// sets a colour scheme.
const QString CounterBackground = QLatin1String("meegotouch-countbubble-background-large");
const QString MutedCounterBackground = QLatin1String("meegotouch-new-items-counter-background-combined");

enum Region { None, Row };

}  // namespace

ChatRow::ChatRow(QDeclarativeItem *parent)
    : ListRow(parent)
{
    setImplicitHeight(RowHeight);
}

void ChatRow::reload()
{
    MEEGRAM_SCOPE("ChatRow::reload");

    const auto *model = qobject_cast<ChatModel *>(m_model);
    if (!model)
        return;

    unwatchFiles();

    m_chatId = QString::number(data(ChatModel::IdRole).toLongLong());
    m_title = data(ChatModel::TitleRole).toString();
    m_isChannel = data(ChatModel::TypeRole).toInt() == Chat::Channel;
    m_isPinned = data(ChatModel::IsPinnedRole).toBool();
    m_isMuted = data(ChatModel::IsMutedRole).toBool();
    m_unreadCount = data(ChatModel::UnreadCountRole).toInt();
    m_mentionCount = data(ChatModel::UnreadMentionCountRole).toInt();
    m_isUnread = m_unreadCount > 0 || data(ChatModel::IsMarkedAsUnreadRole).toBool();

    // isDownloadingCompleted, not localPath: TDLib fills in the path as soon as the
    // download starts, so the path alone points the provider at a half-written file.
    auto *photo = data(ChatModel::PhotoRole).value<File *>();
    watch(photo);

    m_hasPhoto = photo && photo->isDownloadingCompleted();

    if (m_hasPhoto)
        m_avatar.load(ImageLoader::Avatar, photo->localPath(), QSize(AvatarSize, AvatarSize));
    else
        m_avatar.clear();

    downloadOnce(photo);

    const qreal lineWidth = width() - AvatarSize - 44;

    // First line: the channel glyph, the title, the date pinned to the right.
    m_dateText.set(data(ChatModel::DateRole).toString(), labelFont(20, false, true), false, lineWidth, Qt::AlignRight, false);

    const qreal iconWidth = m_isChannel ? 30 : 0;
    const qreal titleWidth = lineWidth - iconWidth - m_dateText.paintedWidth();
    const QFont titleFont = labelFont(26, true);

    // elideEmoji, not replaceEmoji: emoji markup makes this rich text, and a rich line
    // cannot be elided after the fact. A title with no emoji comes back as it was and is
    // elided here as plain text.
    const QString title = Utils::elideEmoji(m_title, titleFont, static_cast<int>(titleWidth));

    m_titleX = TextLeft + iconWidth;
    m_titleText.set(title, titleFont, title != m_title, titleWidth, Qt::AlignLeft, false);
    m_dateX = TextLeft;

    // Second line: the last message, then the mention badge, then the count or the pin.
    qreal right = TextLeft + lineWidth;

    m_countRect = QRectF();
    m_countText.clear();

    if (m_unreadCount > 0)
    {
        m_countText.set(QString::number(m_unreadCount), labelFont(22), false, lineWidth, Qt::AlignLeft, false);

        const qreal countWidth = m_countText.paintedWidth() + 19;
        m_countRect = QRectF(right - countWidth, TitleLineHeight + (SecondLineHeight - BubbleSize) / 2.0, countWidth, BubbleSize);
    }
    else if (m_isPinned)
    {
        m_countRect = QRectF(right - BubbleSize, TitleLineHeight + (SecondLineHeight - BubbleSize) / 2.0, BubbleSize, BubbleSize);
    }

    if (!m_countRect.isNull())
        right = m_countRect.left();

    m_mentionRect = QRectF();

    if (m_mentionCount > 0)
    {
        m_mentionRect = QRectF(right - 8 - BubbleSize, TitleLineHeight + (SecondLineHeight - BubbleSize) / 2.0, BubbleSize, BubbleSize);
        right = m_mentionRect.left() - 8;
    }

    // ChatModel already strips the line breaks and caches the result, so this is plain.
    m_lastMessageText.set(data(ChatModel::LastMessageRole).toString(), labelFont(22, false, true), false, right - TextLeft, Qt::AlignLeft, false);

    update();
}

qlonglong ChatRow::keyAt(int row) const
{
    return m_model ? m_model->data(m_model->index(row, 0), ChatModel::IdRole).toLongLong() : 0;
}

int ChatRow::hitTest(const QPointF &point) const
{
    return boundingRect().contains(point) ? Row : None;
}

void ChatRow::clicked(int region, const QPointF &point)
{
    Q_UNUSED(region);
    Q_UNUSED(point);

    act("openChat", QVariantList() << m_chatId);
}

void ChatRow::held(int region, const QPointF &point)
{
    Q_UNUSED(region);
    Q_UNUSED(point);

    act("openMenu", QVariantList() << m_chatId << m_title << m_isPinned << m_isMuted << m_isUnread);
}

void ChatRow::imageReady(const QString &key)
{
    if (m_avatar.take(key))
        update();
}

void ChatRow::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("ChatRow::paint");

    const PainterGuard guard(painter);
    const auto &palette = Palette::get(m_inverted);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Harmattan's list-item "down" state: a flat wash while pressed. #8c8c8c at half
    // opacity, the same as ListItem.qml - the theme's background grey is too close to the
    // list to show.
    if (m_pressRegion != None)
        painter->fillRect(boundingRect(), QColor(0x8c, 0x8c, 0x8c, 0x80));

    const QRectF avatarRect(12, (RowHeight - AvatarSize) / 2, AvatarSize, AvatarSize);

    if (m_hasPhoto)
    {
        if (!m_avatar.image.isNull())
            painter->drawImage(avatarRect, m_avatar.image);
    }
    else
    {
        // The `l` placeholder has no inverse variant, so the dark theme takes the `m` one -
        // see appWindow.avatarPlaceholder.
        const QPixmap placeholder =
            themePixmap(this, m_inverted ? QLatin1String("icon-m-content-avatar-placeholder-inverse") : QLatin1String("icon-l-content-avatar-placeholder"));
        painter->drawPixmap(avatarRect.toRect(), placeholder);
    }

    if (m_isChannel)
    {
        painter->setFont(iconFont(24));
        painter->setPen(palette.secondary);
        painter->drawText(QRectF(TextLeft, 0, 30, TitleLineHeight), Qt::AlignLeft | Qt::AlignVCenter, ChannelGlyph);
    }

    m_titleText.draw(painter, QPointF(m_titleX, (TitleLineHeight - m_titleText.height()) / 2), palette.text);
    m_dateText.draw(painter, QPointF(m_dateX, (TitleLineHeight - m_dateText.height()) / 2), palette.secondary);
    m_lastMessageText.draw(painter, QPointF(TextLeft, TitleLineHeight + (SecondLineHeight - m_lastMessageText.height()) / 2), palette.secondary);

    if (m_unreadCount > 0)
    {
        drawBorderImage(painter, m_countRect, themePixmap(this, m_isMuted ? MutedCounterBackground : CounterBackground), 10);
        m_countText.draw(painter,
                         QPointF(m_countRect.left() + (m_countRect.width() - m_countText.paintedWidth()) / 2,
                                 m_countRect.top() + 1 + (m_countRect.height() - m_countText.height()) / 2),
                         Qt::white);
    }
    else if (m_isPinned)
    {
        drawBorderImage(painter, m_countRect, themePixmap(this, MutedCounterBackground), 10);
        painter->setFont(iconFont(32));
        painter->setPen(Qt::white);
        painter->drawText(m_countRect, Qt::AlignCenter, PinnedGlyph);
    }

    if (m_mentionCount > 0)
    {
        drawBorderImage(painter, m_mentionRect, themePixmap(this, CounterBackground), 10);
        painter->setFont(iconFont(22));
        painter->setPen(Qt::white);
        painter->drawText(m_mentionRect, Qt::AlignCenter, MentionGlyph);
    }
}
