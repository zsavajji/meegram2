#pragma once

#include "ListRow.hpp"

// One person or public group in a SearchModel list - what the ListItem delegate on
// NewChatPage and NewGroupPage used to build out of eight items and about twenty-five
// bindings each. The same trade as ChatRow: one painted item that reads its model row in
// C++, lays its two lines out once, and paints.
//
// This list is the one that most wanted it. An empty query lists every contact, so it is
// long; and unlike the chat list, most of its avatars are being downloaded for the first
// time while it is on screen, so its rows are the ones that actually re-read.
//
// Calls on `actions`: activate(contactId), download(fileId).
class ContactRow : public rows::ListRow
{
    Q_OBJECT

    // The member picker: draws a tick and lets a tap mean "choose this person". The tap
    // goes out under the same name either way - which of the two it means is the page's
    // business, not the row's - so this only decides whether the tick is drawn and how
    // much room the text leaves for it.
    Q_PROPERTY(bool selectable READ selectable WRITE setSelectable)

public:
    explicit ContactRow(QDeclarativeItem *parent = nullptr);

    bool selectable() const noexcept;
    void setSelectable(bool selectable);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    void reload() override;
    qlonglong keyAt(int row) const override;
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;
    void imageReady(const QString &key) override;

private:
    QString m_contactId;
    QString m_title;
    bool m_selectable{false};
    bool m_isSelected{false};

    // Whether the avatar is a downloaded photo. Until it is, the placeholder is drawn;
    // while one is decoding, nothing is - the way an Image waiting on its source drew.
    bool m_hasPhoto{false};
    rows::RowImage m_avatar;

    rows::TextBlock m_titleText;
    rows::TextBlock m_usernameText;

    // Layout, from reload(): where the two lines start. The pair is centred as a block,
    // the way the Column it replaced was, so a contact with no username is one line
    // centred on its own rather than one line sitting high.
    qreal m_titleY{0};
    qreal m_usernameY{0};
};
