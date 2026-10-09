#include "EmojiGrid.hpp"

#include "ScopeTimer.hpp"
#include "Utils.hpp"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <cmath>

using namespace rows;

namespace {

// Ten to a row in portrait, on 32px assets. Below the platform's touch target, but a
// grid of full-size buttons fits twenty emoji on screen and turns picking one into
// scrolling.
constexpr int CellSize = 48;
constexpr int GlyphSize = 32;

}  // namespace

EmojiGrid::EmojiGrid(QDeclarativeItem *parent)
    : PressItem(parent)
{
    // Asks for exposedRect, so only the cells in view are drawn.
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
}

void EmojiGrid::show(int firstCategory, int lastCategory)
{
    MEEGRAM_SCOPE("EmojiGrid::show");

    m_glyphs.clear();

    // Utils caches each category, so a tab seen before costs the copy and nothing else.
    for (int category = firstCategory; category <= lastCategory; ++category)
    {
        for (const auto &value : Utils::emojiCategory(category))
        {
            const auto entry = value.toMap();
            m_glyphs.push_back({entry.value(QLatin1String("unicode")).toString(), entry.value(QLatin1String("filename")).toString()});
        }
    }

    resize();
    update();
}

int EmojiGrid::columns() const
{
    return qMax(1, static_cast<int>(width()) / CellSize);
}

void EmojiGrid::resize()
{
    const int rows = (static_cast<int>(m_glyphs.size()) + columns() - 1) / columns();
    setImplicitHeight(rows * CellSize);
}

void EmojiGrid::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("EmojiGrid::paint");

    if (m_glyphs.empty())
        return;

    const int perRow = columns();
    const QRectF exposed = option->exposedRect;
    const int firstRow = qMax(0, static_cast<int>(exposed.top()) / CellSize);
    const int lastRow = static_cast<int>(std::ceil(exposed.bottom() / CellSize));
    const int count = static_cast<int>(m_glyphs.size());
    const int inset = (CellSize - GlyphSize) / 2;

    for (int index = firstRow * perRow; index < qMin(count, (lastRow + 1) * perRow); ++index)
    {
        // Native size, decoded once and shared with the emoji in message text.
        painter->drawPixmap((index % perRow) * CellSize + inset, (index / perRow) * CellSize + inset, emojiPixmap(m_glyphs[index].file));
    }
}

int EmojiGrid::hitTest(const QPointF &point) const
{
    if (point.x() < 0 || point.y() < 0 || point.x() >= columns() * CellSize)
        return 0;

    const int index = static_cast<int>(point.y()) / CellSize * columns() + static_cast<int>(point.x()) / CellSize;

    // Regions are 1-based; 0 lets the press through to the Flickable.
    return index < static_cast<int>(m_glyphs.size()) ? index + 1 : 0;
}

void EmojiGrid::clicked(int region, const QPointF &point)
{
    Q_UNUSED(point);

    if (region >= 1 && region <= static_cast<int>(m_glyphs.size()))
        emit picked(m_glyphs[region - 1].unicode);
}

void EmojiGrid::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    PressItem::geometryChanged(newGeometry, oldGeometry);

    // A rotation changes how many fit on a row, and so how many rows there are.
    if (newGeometry.width() != oldGeometry.width())
        resize();
}
