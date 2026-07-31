#include "ChatPhotoProvider.hpp"

#include "ScopeTimer.hpp"

#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImageReader>
#include <QPainter>

namespace {

// Avatars are re-requested constantly: the list keeps several screens of items alive
// and QML's own pixmap store evicts long before a chat list this size is exhausted, so
// every recycle used to re-read and re-decode the same file. Keyed by "path@WxH"
// because the same source is asked for at two sizes (chat list row vs chat header).
//
// This is the second cache, behind QDeclarativePixmapStore - which is ~2 MiB in Qt 4.7
// with no public setter, so it cannot be sized and will thrash on a long list whatever
// we do. What this one decides is what a miss up there costs: an implicitly-shared
// QImage copy, or a JPEG decode plus scale plus mask. Measured at 64 entries: a warm
// re-scroll that took one row-format miss still paid 205 decodes at 83 ms - 17.1
// seconds, against 32.3 ms for every ChatModel::data call in the same window
// (docs/profiling.md). 256 entries is ~4 MiB at 64x64 ARGB32.
//
// ponytail: fixed entry cap rather than a byte budget, so this is only right while
// everything through here is avatar-sized; switch to a cost-based QCache if larger
// images ever use this provider.
constexpr int MaxCachedAvatars = 256;

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

// Masked avatars kept as raw ARGB32_Premultiplied, one file per source-and-size. The
// cache above only lives as long as the process: every restart re-read, re-decoded,
// re-scaled and re-masked every avatar the list showed, at ~39 ms each
// (docs/profiling.md). This turns that into a read of 16 KiB at 64x64, with no codec on
// the way back in.
//
// Raw rather than PNG deliberately - a PNG would mean an encode now and a decode later,
// and the decode is the whole thing being avoided. Disk is the cheap resource here.
//
// ponytail: never pruned. 16 KiB per avatar per size means a few hundred chats is
// single-digit megabytes, and a stale entry is unreachable rather than wrong, because
// TDLib hands out a new path when a chat photo changes. Prune by mtime if a chat list
// ever gets big enough to matter.
QString diskCachePath(const QString &cacheKey)
{
    static const QString directory = [] {
        const QString path = QDir::homePath() + QLatin1String("/.meegram/avatars");
        QDir().mkpath(path);
        return path;
    }();

    const auto digest = QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Md5).toHex();

    return directory + QLatin1Char('/') + QString::fromLatin1(digest) + QLatin1String(".argb");
}

// The file holds the image's own buffer and nothing else, so the geometry has to come
// from the caller. A mismatch means the file is ignored and rewritten rather than
// trusted - which is also what makes a half-written file harmless.
QImage readDiskCache(const QString &path, const QSize &size)
{
    MEEGRAM_SCOPE("ChatPhotoProvider::diskRead");

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QImage();

    const qint64 expected = image.byteCount();
    if (file.size() != expected)
        return QImage();

    if (file.read(reinterpret_cast<char *>(image.bits()), expected) != expected)
        return QImage();

    return image;
}

void writeDiskCache(const QString &path, const QImage &image)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        return;

    // Written to a temporary and renamed, because a torn write of the right length is
    // indistinguishable from a good one on the way back in.
    const QString temporary = path + QLatin1String(".part");

    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    const qint64 expected = image.byteCount();

    if (file.write(reinterpret_cast<const char *>(image.constBits()), expected) != expected)
    {
        file.close();
        QFile::remove(temporary);
        return;
    }

    file.close();

    QFile::remove(path);
    QFile::rename(temporary, path);
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

    // Only when the caller named a size: without one there is no geometry to validate
    // the stored buffer against, and the decode below is what decides the size anyway.
    const bool diskCacheable = requestedSize.isValid() && !requestedSize.isEmpty();
    const QString diskPath = diskCacheable ? diskCachePath(cacheKey) : QString();

    if (diskCacheable)
    {
        if (QImage stored = readDiskCache(diskPath, requestedSize); !stored.isNull())
        {
            if (size)
                *size = stored.size();

            avatarCache().insert(cacheKey, new QImage(stored));

            return stored;
        }
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

    // Nested inside requestImage's scope, so the difference between the two rows is the
    // crop, the mask and the cache insert. 83 ms a call for an ~11 KiB file is high
    // enough not to assume which half it is - and Qt 4.7's JPEG handler only honours
    // setScaledSize at libjpeg's DCT fractions, so an awkward target size means a full
    // decode followed by a smooth scale.
    QImage image;
    {
        MEEGRAM_SCOPE("ChatPhotoProvider::decode");
        image = reader.read();
    }

    if (image.isNull())
        return QImage();

    if (wantsSize && image.size() != requestedSize)
    {
        const QPoint topLeft((image.width() - requestedSize.width()) / 2, (image.height() - requestedSize.height()) / 2);

        image = image.copy(QRect(topLeft, requestedSize));
    }

    image = applyAvatarMask(image);

    // Only when the crop actually landed on the requested geometry: a source smaller
    // than the request is left at its own size, and storing that would be read back as
    // the wrong shape.
    if (diskCacheable && image.size() == requestedSize)
        writeDiskCache(diskPath, image);

    if (size)
        *size = image.size();

    avatarCache().insert(cacheKey, new QImage(image));

    return image;
}
