#include "ChatPhotoProvider.hpp"

#include "ScopeTimer.hpp"

#include <QCache>
#include <QImageReader>

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

    // Decode straight to the requested size rather than decoding at full resolution
    // and letting QML scale afterwards. requestedSize was previously ignored
    // outright, so a large avatar was fully decoded only to be drawn at 64x64.
    if (requestedSize.isValid() && !requestedSize.isEmpty() && sourceSize.isValid() && !sourceSize.isEmpty())
    {
        QSize target = sourceSize;
        target.scale(requestedSize, Qt::KeepAspectRatio);

        if (!target.isEmpty() && target != sourceSize)
        {
            reader.setScaledSize(target);
        }
    }

    const QImage image = reader.read();
    if (image.isNull())
        return QImage();

    if (size)
        *size = image.size();

    avatarCache().insert(cacheKey, new QImage(image));

    return image;
}
