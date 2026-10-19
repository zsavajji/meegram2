#pragma once

#include "ListRow.hpp"

// One forum topic in a TopicListPage, painted - ChatRow's geometry number for number, so
// a forum's topic list reads as the chat list it stands in for. Reads a ForumTopicModel
// row.
//
// Its own item rather than ChatRow reused: a topic has no photo and no File behind one,
// so the whole avatar path (watch, downloadOnce, ImageLoader, the theme placeholder) has
// nothing to do here and is replaced by a coloured circle with the topic's initial.
// Sharing ChatRow instead would have meant a second avatar mode inside it plus two model
// role enums that must be declared in the same order to stay compatible - the kind of
// implicit coupling that breaks quietly. The reuse that matters is rows::ListRow, which
// is where the press handling, the text layout and the palette already live.
//
// Calls on `actions`: openTopic(topicId, name).
class TopicRow : public rows::ListRow
{
    Q_OBJECT

public:
    explicit TopicRow(QDeclarativeItem *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    void reload() override;
    qlonglong keyAt(int row) const override;
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;
    void imageReady(const QString &key) override;

private:
    QString m_topicId;
    QString m_name;
    QColor m_iconColor;
    bool m_isClosed{false};
    bool m_isPinned{false};
    int m_unreadCount{0};
    int m_mentionCount{0};

    // The circle's letter: the first character of the topic's name, which is what
    // Telegram draws for a topic with no custom emoji.
    QString m_initial;

    rows::TextBlock m_nameText;
    rows::TextBlock m_dateText;
    rows::TextBlock m_lastMessageText;
    rows::TextBlock m_countText;

    // Layout, from reload(): where the name and date boxes start, and the two bubbles at
    // the end of the second line.
    qreal m_nameX{0};
    QRectF m_countRect;
    QRectF m_mentionRect;
};
