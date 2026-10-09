#include "MemberList.hpp"

#include "ChatManager.hpp"
#include "File.hpp"
#include "ScopeTimer.hpp"
#include "Utils.hpp"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

using namespace rows;

namespace {

// The QML row's geometry: 76px rows, a 56px avatar, the text 12px past it.
constexpr int RowHeight = 76;
constexpr int AvatarSize = 56;
constexpr int TextLeft = AvatarSize + 12;

}  // namespace

MemberList::MemberList(QDeclarativeItem *parent)
    : PressItem(parent)
{
    // Asks for exposedRect: the list is up to 15,200px tall and only a screenful of it is
    // ever on show, so that is what gets drawn.
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);

    connect(ImageLoader::instance(), SIGNAL(ready(QString)), this, SLOT(handleImageReady(QString)));
}

QObject *MemberList::formatter() const noexcept
{
    return m_info;
}

void MemberList::setFormatter(QObject *formatter)
{
    auto *info = qobject_cast<ChatInfoFormatter *>(formatter);

    if (info == m_info)
        return;

    if (m_info)
        disconnect(m_info, nullptr, this, nullptr);

    m_info = info;

    if (m_info)
        connect(m_info, SIGNAL(membersChanged()), this, SLOT(reload()));

    reload();
}

int MemberList::count() const noexcept
{
    return static_cast<int>(m_members.size());
}

void MemberList::reload()
{
    MEEGRAM_SCOPE("MemberList::reload");

    for (const auto &member : m_members)
    {
        if (member.photo)
            disconnect(member.photo, nullptr, this, nullptr);
    }

    m_members.clear();

    const QVariantList rows = m_info ? m_info->members() : QVariantList();
    m_members.resize(rows.size());

    for (int i = 0; i < rows.size(); ++i)
    {
        const auto row = rows.at(i).toMap();
        auto &member = m_members[i];

        member.userId = row.value(QLatin1String("userId")).toString();
        member.name = row.value(QLatin1String("name")).toString();
        member.tag = row.value(QLatin1String("tag")).toString();
        member.status = row.value(QLatin1String("status")).toString();
        member.photo = row.value(QLatin1String("photo")).value<File *>();

        // The avatar appears when its download lands. StorageManager holds the File for
        // the session, so it outlives this list either way.
        if (member.photo)
            connect(member.photo, SIGNAL(fileChanged()), this, SLOT(handleFileChanged()), Qt::UniqueConnection);
    }

    setImplicitHeight(RowHeight * count());
    emit countChanged();
    update();
}

void MemberList::layOut(Member &member) const
{
    const qreal textWidth = width() - TextLeft;
    const QFont nameFont = labelFont(26);

    // The rank on the right, then the name up to it. elideEmoji, not replaceEmoji: emoji
    // markup makes this rich text, and rich text cannot be elided after the fact.
    member.tagText.set(member.tag, labelFont(18, false, true), false, textWidth, Qt::AlignRight, false);

    const qreal nameWidth = textWidth - (member.tagText.isEmpty() ? 0 : member.tagText.paintedWidth() + 8);
    const QString name = Utils::elideEmoji(member.name, nameFont, static_cast<int>(nameWidth));

    member.nameText.set(name, nameFont, name != member.name, nameWidth, Qt::AlignLeft, false);
    member.statusText.set(member.status, labelFont(20, false, true), false, textWidth, Qt::AlignLeft, false);
    member.laidOut = true;
}

void MemberList::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("MemberList::paint");

    if (m_members.empty() || width() <= 0)
        return;

    const PainterGuard guard(painter);
    const auto &palette = Palette::get(m_inverted);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF exposed = option->exposedRect;
    const int first = qMax(0, static_cast<int>(exposed.top()) / RowHeight);
    const int last = qMin(count() - 1, static_cast<int>(exposed.bottom()) / RowHeight);

    for (int i = first; i <= last; ++i)
    {
        auto &member = m_members[i];
        const qreal top = i * RowHeight;

        if (!member.laidOut)
            layOut(member);

        // Same provider as the chat list, so it arrives cropped, masked and cached at
        // this size. Fetched once the row is on screen: the people scrolled to, not all
        // two hundred.
        File *photo = member.photo;
        const QRectF avatarRect(0, top + (RowHeight - AvatarSize) / 2, AvatarSize, AvatarSize);

        if (photo && photo->isDownloadingCompleted())
        {
            member.avatar.load(ImageLoader::Avatar, photo->localPath(), QSize(AvatarSize, AvatarSize));

            if (!member.avatar.image.isNull())
                painter->drawImage(avatarRect, member.avatar.image);
        }
        else
        {
            painter->drawPixmap(avatarRect.toRect(), themePixmap(this, m_inverted ? QLatin1String("icon-m-content-avatar-placeholder-inverse")
                                                                                  : QLatin1String("icon-l-content-avatar-placeholder")));

            if (photo && photo->canBeDownloaded() && !photo->isDownloadingActive() && !m_requested.contains(photo->id()))
            {
                m_requested.insert(photo->id());
                act("download", QVariantList() << photo->id());
            }
        }

        // The name and the line under it, centred on the row as one column.
        const qreal columnHeight = member.nameText.height() + 2 + member.statusText.height();
        const qreal nameTop = top + (RowHeight - columnHeight) / 2;

        member.nameText.draw(painter, QPointF(TextLeft, nameTop), palette.text);
        member.tagText.draw(painter, QPointF(TextLeft, nameTop + member.nameText.ascent() - member.tagText.ascent()), palette.secondary);
        member.statusText.draw(painter, QPointF(TextLeft, nameTop + member.nameText.height() + 2), palette.secondary);
    }
}

int MemberList::hitTest(const QPointF &point) const
{
    const int row = static_cast<int>(point.y()) / RowHeight;

    // Regions are 1-based; 0 lets the press through to the page.
    return point.y() >= 0 && row < count() ? row + 1 : 0;
}

void MemberList::clicked(int region, const QPointF &point)
{
    Q_UNUSED(point);

    if (region >= 1 && region <= count())
        act("openProfile", QVariantList() << m_members[region - 1].userId);
}

void MemberList::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    PressItem::geometryChanged(newGeometry, oldGeometry);

    if (newGeometry.width() == oldGeometry.width())
        return;

    for (auto &member : m_members)
        member.laidOut = false;

    update();
}

void MemberList::handleImageReady(const QString &key)
{
    for (auto &member : m_members)
    {
        if (member.avatar.take(key))
        {
            update();
            return;
        }
    }
}

void MemberList::handleFileChanged()
{
    update();
}
