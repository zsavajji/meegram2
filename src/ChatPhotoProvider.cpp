#include "ChatPhotoProvider.hpp"

#include "ScopeTimer.hpp"

#include <QCache>
#include <QHash>
#include <QImageReader>
#include <QPainter>

namespace {

// Avatars are re-requested constantly: the delegates set cache: false and the list
// keeps several screens of items alive, so every scroll recycle used to re-read and
// re-decode the same file from disk on the GUI thread. Keyed by "path@WxH" because
// the same source is asked for at two sizes (chat list row vs chat header).
//
// ponytail: fixed 64-entry cap rather than a byte budget. At avatar sizes that is
// roughly a megabyte worst case; switch to a cost-based QCache if larger images
// ever go through this provider.
constexpr int MaxCachedAvatars = 64;

QCache<QString, QImage> &avatarCache()
{
    static QCache<QString, QImage> cache(MaxCachedAvatars);
    return cache;
}

// The round avatar cutout, scaled to whatever size is being asked for. Two sizes are
// in play - the chat list row and the chat header - so a tiny map is enough.
QImage avatarMask(const QSize &size)
{
    static QHash<qint64, QImage> masks;

    const auto key = (static_cast<qint64>(size.width()) << 32) | static_cast<quint32>(size.height());

    if (const auto it = masks.constFind(key); it != masks.constEnd())
        return it.value();

    QImage mask(QLatin1String(":/images/avatar-image-mask.png"));

    if (!mask.isNull() && mask.size() != size)
        mask = mask.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    masks.insert(key, mask);

    return mask;
}

// Was a MaskedItem per delegate in QML, so the cutout was applied live on every
// visible row. The shape is the same for every avatar, so it belongs here: composited
// once per image and kept in the cache below, instead of once per row.
//
// The mask defines its shape in the alpha channel, which is exactly what
// DestinationIn consumes.
QImage applyAvatarMask(QImage image)
{
    const auto mask = avatarMask(image.size());
    if (mask.isNull())
        return image;

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();

    return image;
}

}  // namespace

QImage ChatPhotoProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    MEEGRAM_SCOPE("ChatPhotoProvider::requestImage");

    const auto cacheKey = QString("%1@%2x%3").arg(id, QString::number(requestedSize.width()), QString::number(requestedSize.height()));

    if (const auto *cached = avatarCache().object(cacheKey))
    {
        if (size)
            *size = cached->size();

        return *cached;
    }

    QImageReader reader(id);
    if (!reader.canRead())
        return QImage();

    const QSize sourceSize = reader.size();
    const auto wantsSize = requestedSize.isValid() && !requestedSize.isEmpty() && sourceSize.isValid() && !sourceSize.isEmpty();

    // Decode straight to the requested size rather than decoding at full resolution
    // and letting QML scale afterwards. requestedSize was previously ignored
    // outright, so a large avatar was fully decoded only to be drawn at 64x64.
    //
    // Expanding, not KeepAspectRatio: the delegate used fillMode PreserveAspectCrop,
    // and that crop has to happen before the mask or a non-square avatar gets a
    // clipped circle.
    if (wantsSize)
    {
        QSize target = sourceSize;
        target.scale(requestedSize, Qt::KeepAspectRatioByExpanding);

        if (!target.isEmpty() && target != sourceSize)
        {
            reader.setScaledSize(target);
        }
    }

    QImage image = reader.read();
    if (image.isNull())
        return QImage();

    if (wantsSize && image.size() != requestedSize)
    {
        const QPoint topLeft((image.width() - requestedSize.width()) / 2, (image.height() - requestedSize.height()) / 2);

        image = image.copy(QRect(topLeft, requestedSize));
    }

    image = applyAvatarMask(image);

    if (size)
        *size = image.size();

    avatarCache().insert(cacheKey, new QImage(image));

    return image;
}
