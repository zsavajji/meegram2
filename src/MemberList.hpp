#pragma once

#include "ListRow.hpp"

#include <vector>

class ChatInfoFormatter;
class File;

// The members section of ProfilePage, painted as one item. It was a Repeater, which built
// all of up to 200 rows - avatar, three labels, a MouseArea - on every visit, and asked
// TDLib for every one of their avatars on the spot. This lays a row out and fetches its
// avatar only once it is first drawn, and draws only the rows in view.
//
// Reads ChatInfoFormatter::members() directly rather than through a QML binding, which
// would turn the whole list into a JS array and back on the way in.
//
// Calls on `actions`: openProfile(userId), download(fileId).
class MemberList : public rows::PressItem
{
    Q_OBJECT

    // The page's chatInfo. Not called `info`: ProfilePage has an item with that id.
    Q_PROPERTY(QObject *formatter READ formatter WRITE setFormatter)

    // How many rows there are, for the section around this to hide itself on.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit MemberList(QDeclarativeItem *parent = nullptr);

    QObject *formatter() const noexcept;
    void setFormatter(QObject *formatter);

    int count() const noexcept;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void countChanged();

protected:
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;

    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    void reload();
    void handleImageReady(const QString &key);
    void handleFileChanged();

private:
    struct Member
    {
        QString userId;
        QString name;
        QString tag;
        QString status;
        QPointer<File> photo;

        // Laid out the first time the row is drawn, at the width it is drawn at.
        bool laidOut{false};
        rows::TextBlock nameText;
        rows::TextBlock tagText;
        rows::TextBlock statusText;
        rows::RowImage avatar;
    };

    void layOut(Member &member) const;

    QPointer<ChatInfoFormatter> m_info;
    std::vector<Member> m_members;

    // Avatars asked for, so a failed download is not re-requested on every paint.
    QSet<int> m_requested;
};
