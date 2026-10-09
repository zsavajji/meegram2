#pragma once

#include "ListRow.hpp"

#include <vector>

// The emoji picker's grid, painted as one item inside a Flickable. It was a GridView over
// a JS array: every tab switch converted a category's few hundred { unicode, filename }
// maps into JS, and every row scrolled into view built an Item, an Image and a MouseArea
// per cell. This reads the category in C++ and draws only the cells on screen, from
// the emoji cache the message text shares.
class EmojiGrid : public rows::PressItem
{
    Q_OBJECT

public:
    explicit EmojiGrid(QDeclarativeItem *parent = nullptr);

    // Emoji::Category first to last, inclusive - a tab can merge two neighbours.
    Q_INVOKABLE void show(int firstCategory, int lastCategory);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void picked(const QString &unicode);

protected:
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;

    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Glyph
    {
        QString unicode;
        QString file;
    };

    int columns() const;
    void resize();

    std::vector<Glyph> m_glyphs;
};
