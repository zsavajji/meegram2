#pragma once

#include <QDeclarativeImageProvider>

// Decodes WebP stickers, which Qt 4.7 cannot: it has no WebP image handler and the
// device ships none, so a plain Image on a .webp file fails and the sticker delegate
// falls back to its emoji.
//
// A provider rather than a QImageIOPlugin. WebP only ever arrives here as a sticker,
// so app-wide QImageReader support would buy nothing, and a static plugin that fails
// to register reports itself as "unsupported format" with nothing to debug.
class StickerProvider : public QDeclarativeImageProvider
{
public:
    StickerProvider()
        : QDeclarativeImageProvider(QDeclarativeImageProvider::Image)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
