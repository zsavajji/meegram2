#pragma once

#include "ListRow.hpp"

// One chat in a ChatListView, painted - what ChatItem.qml used to build out of a dozen
// items. Reads a ChatModel row.
//
// Calls on `actions`: openChat(chatId), openMenu(chatId, title, pinned, muted, unread),
// download(fileId).
class ChatRow : public rows::ListRow
{
    Q_OBJECT

public:
    explicit ChatRow(QDeclarativeItem *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    void reload() override;
    qlonglong keyAt(int row) const override;
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;
    void held(int region, const QPointF &point) override;
    void imageReady(const QString &key) override;

private:
    QString m_chatId;
    QString m_title;
    bool m_isChannel{false};
    bool m_isPinned{false};
    bool m_isMuted{false};
    bool m_isUnread{false};
    int m_unreadCount{0};
    int m_mentionCount{0};

    // Whether the avatar is a downloaded photo. Until it is, the placeholder is drawn;
    // while one is decoding, nothing is - the way an Image waiting on its source drew.
    bool m_hasPhoto{false};
    rows::RowImage m_avatar;

    rows::TextBlock m_titleText;
    rows::TextBlock m_dateText;
    rows::TextBlock m_lastMessageText;
    rows::TextBlock m_countText;

    // Layout, from reload(): where the title, date and last message boxes start, and the
    // two bubbles at the end of the second line.
    qreal m_titleX{0};
    qreal m_dateX{0};
    QRectF m_countRect;
    QRectF m_mentionRect;
};
