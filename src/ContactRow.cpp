#include "ContactRow.hpp"

#include "File.hpp"
#include "ScopeTimer.hpp"
#include "SearchModel.hpp"
#include "Utils.hpp"

#include <QPainter>

using namespace rows;

namespace {

// ListItem.qml's geometry: 64 + PADDING_DOUBLE * 2 of height, a 64px avatar 12px in, and
// the text column 16px past it. Which comes out as ChatRow's row, number for number.
constexpr int RowHeight = 88;
constexpr int AvatarSize = 64;
constexpr int TextLeft = 12 + AvatarSize + 16;
constexpr int RightMargin = 12;

enum Region { None, Row };

}  // namespace

ContactRow::ContactRow(QDeclarativeItem *parent)
    : ListRow(parent)
{
    setImplicitHeight(RowHeight);
}

bool ContactRow::selectable() const noexcept
{
    return m_selectable;
}

void ContactRow::setSelectable(bool selectable)
{
    if (m_selectable == selectable)
        return;

    m_selectable = selectable;

    reloadIfComplete();
}

void ContactRow::reload()
{
    MEEGRAM_SCOPE("ContactRow::reload");

    if (!m_model)
        return;

    unwatchFiles();

    // Already a string from this model, unlike every other id role in this codebase -
    // see SearchModel::data.
    m_contactId = data(SearchModel::IdRole).toString();
    m_title = data(SearchModel::TitleRole).toString();
    m_isSelected = data(SearchModel::SelectedRole).toBool();

    // isDownloadingCompleted, not localPath: TDLib fills in the path as soon as the
    // download starts, so the path alone points the provider at a half-written file.
    auto *photo = data(SearchModel::PhotoRole).value<File *>();
    watch(photo);

    m_hasPhoto = photo && photo->isDownloadingCompleted();

    if (m_hasPhoto)
        m_avatar.load(ImageLoader::Avatar, photo->localPath(), QSize(AvatarSize, AvatarSize));
    else
        m_avatar.clear();

    downloadOnce(photo);

    // The tick's own width plus its margin, taken off the text before anything is laid
    // out - so a long name elides in front of it rather than under it.
    const QPixmap tick = m_selectable ? themePixmap(this, QLatin1String("meegotouch-button-checkbox-background")) : QPixmap();
    const qreal tickWidth = tick.isNull() ? 0 : tick.width() + RightMargin;

    const qreal lineWidth = width() - TextLeft - RightMargin - tickWidth;

    const QFont titleFont = labelFont(26, true);

    // elideEmoji, not replaceEmoji: emoji markup makes this rich text, and a rich line
    // cannot be elided after the fact. Same call and same reason as ChatRow's title.
    const QString title = Utils::elideEmoji(m_title, titleFont, static_cast<int>(lineWidth));

    m_titleText.set(title, titleFont, title != m_title, lineWidth, Qt::AlignLeft, false);

    // A contact reached by phone number has no username, and the row is one line then -
    // which the Column this replaced expressed by leaving an invisible child out.
    m_usernameText.set(data(SearchModel::UsernameRole).toString(), labelFont(20, false, true), false, lineWidth, Qt::AlignLeft, false);

    const qreal usernameHeight = m_usernameText.isEmpty() ? 0 : m_usernameText.height();
    const qreal total = m_titleText.height() + usernameHeight;

    m_titleY = (RowHeight - total) / 2;
    m_usernameY = m_titleY + m_titleText.height();

    update();
}

qlonglong ContactRow::keyAt(int row) const
{
    // toLongLong on the string this model's id role returns, which parses it exactly.
    return m_model ? m_model->data(m_model->index(row, 0), SearchModel::IdRole).toLongLong() : 0;
}

int ContactRow::hitTest(const QPointF &point) const
{
    return boundingRect().contains(point) ? Row : None;
}

void ContactRow::clicked(int region, const QPointF &point)
{
    Q_UNUSED(region);
    Q_UNUSED(point);

    act("activate", QVariantList() << m_contactId);
}

void ContactRow::imageReady(const QString &key)
{
    if (m_avatar.take(key))
        update();
}

void ContactRow::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    MEEGRAM_SCOPE("ContactRow::paint");

    const PainterGuard guard(painter);
    const auto &palette = Palette::get(m_inverted);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ListItem.qml's two highlight states, which were a Loader and a Rectangle: #8c8c8c
    // at half opacity while pressed, white at half opacity while chosen. The pressed one
    // wins, so a chosen row still answers a touch.
    if (m_pressRegion != None)
        painter->fillRect(boundingRect(), QColor(0x8c, 0x8c, 0x8c, 0x80));
    else if (m_isSelected)
        painter->fillRect(boundingRect(), QColor(0xff, 0xff, 0xff, 0x80));

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

    m_titleText.draw(painter, QPointF(TextLeft, m_titleY), palette.text);

    if (!m_usernameText.isEmpty())
        m_usernameText.draw(painter, QPointF(TextLeft, m_usernameY), palette.secondary);

    if (m_selectable)
    {
        // The same pair LanguageSettingsPage uses, which are known to exist in the
        // Harmattan theme.
        const QPixmap tick = themePixmap(this, m_isSelected ? QLatin1String("meegotouch-button-radiobutton-background-selected")
                                                            : QLatin1String("meegotouch-button-checkbox-background"));

        if (!tick.isNull())
            painter->drawPixmap(QPointF(width() - RightMargin - tick.width(), (RowHeight - tick.height()) / 2.0), tick);
    }
}
