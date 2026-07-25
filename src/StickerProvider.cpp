#include "StickerProvider.hpp"

#include "ScopeTimer.hpp"

#include <QCache>
#include <QFile>

#include <webp/decode.h>

namespace {

// Stickers are nominally 512x512, which is 1 MB each as ARGB32. The delegate draws
// them around 180px, so a byte budget rather than a count: this is roughly a dozen
// stickers at display size, and it cannot be blown open by one large one.
constexpr int MaxCachedStickerKb = 1536;

QCache<QString, QImage> &stickerCache()
{
    // cost() is in KB, matching MaxCachedStickerKb.
    static QCache<QString, QImage> cache(MaxCachedStickerKb);
    return cache;
}

}  // namespace

QImage StickerProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    MEEGRAM_SCOPE("StickerProvider::requestImage");

    const auto cacheKey = QString("%1@%2x%3").arg(id, QString::number(requestedSize.width()), QString::number(requestedSize.height()));

    if (const auto *cached = stickerCache().object(cacheKey))
    {
        if (size)
            *size = cached->size();

        return *cached;
    }

    QFile file(id);
    if (!file.open(QIODevice::ReadOnly))
        return QImage();

    const auto encoded = file.readAll();
    file.close();

    const auto *data = reinterpret_cast<const uint8_t *>(encoded.constData());
    const auto length = static_cast<size_t>(encoded.size());

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config))
        return QImage();

    if (WebPGetFeatures(data, length, &config.input) != VP8_STATUS_OK)
        return QImage();

    auto width = config.input.width;
    auto height = config.input.height;

    // libwebp can scale during the decode, so a 512px sticker is never fully expanded
    // in memory just to be drawn at 180. Aspect ratio is preserved here rather than
    // left to the delegate, which sizes itself from the sticker's own dimensions.
    if (requestedSize.isValid() && !requestedSize.isEmpty() && width > 0 && height > 0)
    {
        QSize target(width, height);
        target.scale(requestedSize, Qt::KeepAspectRatio);

        if (!target.isEmpty() && (target.width() != width || target.height() != height))
        {
            config.options.use_scaling = 1;
            config.options.scaled_width = target.width();
            config.options.scaled_height = target.height();

            width = target.width();
            height = target.height();
        }
    }

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();

    // MODE_bgrA, not RGBA. BGRA is exactly ARGB32's byte order on a little-endian
    // machine, so the decode lands straight in the QImage with no channel swap; and
    // the lower-case 'b' asks libwebp to premultiply, which is the format Qt's raster
    // engine composites without converting first. Stickers are mostly transparent, so
    // that blend happens on every frame they are drawn.
    config.output.colorspace = MODE_bgrA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = image.bits();
    config.output.u.RGBA.stride = image.bytesPerLine();
    config.output.u.RGBA.size = static_cast<size_t>(image.byteCount());

    const auto status = WebPDecode(data, length, &config);

    WebPFreeDecBuffer(&config.output);

    if (status != VP8_STATUS_OK)
        return QImage();

    if (size)
        *size = image.size();

    stickerCache().insert(cacheKey, new QImage(image), qMax(1, image.byteCount() / 1024));

    return image;
}
