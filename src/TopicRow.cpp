#include "TopicRow.hpp"

#include "ForumTopicModel.hpp"
#include "ScopeTimer.hpp"
#include "Utils.hpp"

#include <QPainter>

using namespace rows;

namespace {

// ChatRow's geometry, so the two lists line up: an 88px row, a 64px circle 12px in, and
// two text lines starting 16px past it.
constexpr int RowHeight = 88;
constexpr int IconSize = 64;
constexpr int TextLeft = 12 + IconSize + 16;
constexpr int TitleLineHeight = 45;
constexpr int SecondLineHeight = 30;
constexpr int BubbleSize = 32;

const QString LockGlyph = QString(QChar(0xe831));
const QString PinnedGlyph = QString(QChar(0xe842));
const QString MentionGlyph = QString(QChar(0xe85a));

// The same two count bubbles ChatRow draws.
const QString CounterBackground = QLatin1String("meegotouch-countbubble-background-large");
const QString MutedCounterBackground = QLatin1String("meegotouch-new-items-counter-background-combined");

enum Region { None, Row };

}  // namespace

TopicRow::TopicRow(QDeclarativeItem *parent)
    : ListRow(parent)
{
    setImplicitHeight(RowHeight);
}

void TopicRow::reload()
{
    MEEGRAM_SCOPE("TopicRow::reload");

    if (!m_model)
        return;

    m_topicId = QString::number(data(ForumTopicModel::IdRole).toInt());
    m_name = data(ForumTopicModel::NameRole).toString();
    m_isClosed = data(ForumTopicModel::IsClosedRole).toBool();
    m_isPinned = data(ForumTopicModel::IsPinnedRole).toBool();
    m_unreadCount = data(ForumTopicModel::UnreadCountRole).toInt();
    m_mentionCount = data(ForumTopicModel::UnreadMentionCountRole).toInt();

    // 0xRRGGBB from the model, already defaulted there for a topic the server sent no
    // colour for.
    m_iconColor = QColor(QRgb(0xff000000 | static_cast<unsigned>(data(ForumTopicModel::IconColorRole).toInt())));

    // The emoji a topic name so often starts with is drawn as a letter here rather than
    // as an image: the circle is 64px of flat colour and one glyph, and decoding a PNG
    // per row to put inside it is what the painted rows exist to avoid.
    m_initial = m_name.left(1).toUpper();

    const qreal lineWidth = width() - IconSize - 44;

    // First line: the lock on a closed topic, the name, the date pinned to the right.
    m_dateText.set(data(ForumTopicModel::DateRole).toString(), labelFont(20, false, true), false, lineWidth, Qt::AlignRight, false);

    const qreal iconWidth = m_isClosed ? 30 : 0;
    const qreal nameWidth = lineWidth - iconWidth - m_dateText.paintedWidth();
    const QFont nameFont = labelFont(26, true);

    // elideEmoji, not replaceEmoji: emoji markup makes this rich text, and a rich line
    // cannot be elided after the fact. Same call and same reason as ChatRow's title.
    const QString name = Utils::elideEmoji(m_name, nameFont, static_cast<int>(nameWidth));

    m_nameX = TextLeft + iconWidth;
    m_nameText.set(name, nameFont, name != m_name, nameWidth, Qt::AlignLeft, false);

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

    // The model already strips the line breaks and caches the result, so this is plain.
    m_lastMessageText.set(data(ForumTopicModel::LastMessageRole).toString(), labelFont(22, false, true), false, right - TextLeft, Qt::AlignLeft,
                          false);

    update();
}

qlonglong TopicRow::keyAt(int row) const
{
    return m_model ? m_model->data(m_model->index(row, 0), ForumTopicModel::IdRole).toLongLong() : 0;
}

int TopicRow::hitTest(const QPointF &point) const
{
    return boundingRect().contains(point) ? Row : None;
}

void TopicRow::clicked(int region, const QPointF &point)
{
    Q_UNUSED(region);
    Q_UNUSED(point);

    // The name rides along: the page pushed for the topic puts it in its header, and the
    // topic is not in any store for it to look up.
    act("openTopic", QVariantList() << m_topicId << m_name);
}

void TopicRow::imageReady(const QString &key)
{
    // Nothing on this row is decoded: the circle is painted and its letter is a glyph.
    Q_UNUSED(key);
}

void TopicRow::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("TopicRow::paint");

    const PainterGuard guard(painter);
    const auto &palette = Palette::get(m_inverted);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Harmattan's list-item "down" state, as ChatRow draws it.
    if (m_pressRegion != None)
        painter->fillRect(boundingRect(), QColor(0x8c, 0x8c, 0x8c, 0x80));

    const QRectF iconRect(12, (RowHeight - IconSize) / 2, IconSize, IconSize);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_iconColor);
    painter->drawEllipse(iconRect);
    painter->restore();

    if (!m_initial.isEmpty())
    {
        painter->setFont(labelFont(32, true));
        painter->setPen(Qt::white);
        painter->drawText(iconRect, Qt::AlignCenter, m_initial);
    }

    if (m_isClosed)
    {
        painter->setFont(iconFont(24));
        painter->setPen(palette.secondary);
        painter->drawText(QRectF(TextLeft, 0, 30, TitleLineHeight), Qt::AlignLeft | Qt::AlignVCenter, LockGlyph);
    }

    m_nameText.draw(painter, QPointF(m_nameX, (TitleLineHeight - m_nameText.height()) / 2), palette.text);
    m_dateText.draw(painter, QPointF(TextLeft, (TitleLineHeight - m_dateText.height()) / 2), palette.secondary);
    m_lastMessageText.draw(painter, QPointF(TextLeft, TitleLineHeight + (SecondLineHeight - m_lastMessageText.height()) / 2), palette.secondary);

    if (m_unreadCount > 0)
    {
        drawBorderImage(painter, m_countRect, themePixmap(this, CounterBackground), 10);
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
