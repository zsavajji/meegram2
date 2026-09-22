#include "QrScanner.hpp"

#include <QDateTime>
#include <QDebug>
#include <QPainter>

#include <quirc.h>

#include <cstring>

namespace {

// How often the decoder actually runs. The viewfinder is 25-30 fps and quirc walks the
// whole image looking for finder patterns, which on a 1 GHz A8 is tens of milliseconds -
// so running it per frame would cost most of a core to find a code that is not going
// anywhere. Aiming a phone at a screen takes longer than this.
constexpr int DecodeIntervalMs = 350;

// The viewfinder is drawn from the same grayscale buffer the decoder reads, rather than
// converting YUV to RGB for display: the luma plane *is* the grayscale image, so this costs
// one pass instead of two, and a monochrome viewfinder is no worse for aiming at a QR code.
QVector<QRgb> grayscaleTable()
{
    QVector<QRgb> table(256);

    for (int i = 0; i < 256; ++i)
        table[i] = qRgb(i, i, i);

    return table;
}

}  // namespace

#ifdef MEEGRAM_QR_SCANNER

// The camera's frames, as an 8-bit luma image. Only the formats this can actually convert
// are advertised, so the backend negotiates one of them or fails outright rather than
// handing over something that would be drawn as noise.
class QrVideoSurface : public QAbstractVideoSurface
{
public:
    explicit QrVideoSurface(QrScanner *scanner)
        : QAbstractVideoSurface(scanner)
        , m_scanner(scanner)
    {
    }

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle)
            return QList<QVideoFrame::PixelFormat>();

        // The YUV ones first: that is what the N9's camera produces, and for all of them
        // the luma is either a plane or every other byte - which is the whole conversion.
        QList<QVideoFrame::PixelFormat> formats;

        formats << QVideoFrame::Format_UYVY << QVideoFrame::Format_YUYV << QVideoFrame::Format_YV12 << QVideoFrame::Format_NV12
                << QVideoFrame::Format_NV21 << QVideoFrame::Format_RGB32 << QVideoFrame::Format_ARGB32 << QVideoFrame::Format_RGB24;

        return formats;
    }

    bool present(const QVideoFrame &frame) override
    {
        QVideoFrame mapped(frame);

        if (!mapped.map(QAbstractVideoBuffer::ReadOnly))
            return false;

        const int width = mapped.width();
        const int height = mapped.height();
        const int stride = mapped.bytesPerLine();
        const uchar *bits = mapped.bits();

        QImage grayscale(width, height, QImage::Format_Indexed8);
        grayscale.setColorTable(grayscaleTable());

        switch (mapped.pixelFormat())
        {
            // One plane of luma, full size, then the chroma planes this ignores.
            case QVideoFrame::Format_YV12:
            case QVideoFrame::Format_NV12:
            case QVideoFrame::Format_NV21:
                for (int y = 0; y < height; ++y)
                    std::memcpy(grayscale.scanLine(y), bits + static_cast<qptrdiff>(y) * stride, width);
                break;

            // Packed 4:2:2. UYVY carries luma in the odd bytes, YUYV in the even ones.
            case QVideoFrame::Format_UYVY:
            case QVideoFrame::Format_YUYV: {
                const int offset = mapped.pixelFormat() == QVideoFrame::Format_UYVY ? 1 : 0;

                for (int y = 0; y < height; ++y)
                {
                    const uchar *line = bits + static_cast<qptrdiff>(y) * stride;
                    uchar *out = grayscale.scanLine(y);

                    for (int x = 0; x < width; ++x)
                        out[x] = line[x * 2 + offset];
                }
                break;
            }

            // Integer luma, the usual 299/587/114 weights without the floating point.
            case QVideoFrame::Format_RGB32:
            case QVideoFrame::Format_ARGB32:
            case QVideoFrame::Format_RGB24: {
                const int step = mapped.pixelFormat() == QVideoFrame::Format_RGB24 ? 3 : 4;

                for (int y = 0; y < height; ++y)
                {
                    const uchar *line = bits + static_cast<qptrdiff>(y) * stride;
                    uchar *out = grayscale.scanLine(y);

                    for (int x = 0; x < width; ++x)
                    {
                        const uchar *pixel = line + x * step;

                        // BGRA on little-endian, which is what Format_RGB32 means in Qt.
                        out[x] = static_cast<uchar>((pixel[2] * 299 + pixel[1] * 587 + pixel[0] * 114) / 1000);
                    }
                }
                break;
            }

            default:
                mapped.unmap();
                return false;
        }

        mapped.unmap();

        m_scanner->takeFrame(grayscale);

        return true;
    }

private:
    QrScanner *m_scanner;
};

#endif

QrScanner::QrScanner(QDeclarativeItem *parent)
    : QDeclarativeItem(parent)
{
    // A QDeclarativeItem draws nothing unless this is cleared.
    setFlag(QGraphicsItem::ItemHasNoContents, false);

    m_quirc = quirc_new();
}

QrScanner::~QrScanner()
{
    // Stops the camera and releases the device before anything it might still be writing
    // into goes away.
    setActive(false);

    if (m_quirc)
        quirc_destroy(m_quirc);
}

bool QrScanner::isActive() const noexcept
{
    return m_active;
}

bool QrScanner::isAvailable() const noexcept
{
#ifdef MEEGRAM_QR_SCANNER
    return true;
#else
    return false;
#endif
}

void QrScanner::setActive(bool active)
{
    if (m_active == active)
        return;

#ifdef MEEGRAM_QR_SCANNER
    if (active)
    {
        if (!m_camera)
        {
            m_camera = new QCamera(this);
            m_surface = new QrVideoSurface(this);

            connect(m_camera, SIGNAL(error(QCamera::Error)), SLOT(handleCameraError()));

            m_camera->setViewfinder(m_surface);
            m_camera->setCaptureMode(QCamera::CaptureStillImage);
        }

        m_camera->start();
    }
    else if (m_camera)
    {
        m_camera->stop();
    }
#endif

    m_active = active;

    if (!m_active)
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = QImage();
    }

    emit activeChanged();

    update();
}

void QrScanner::takeFrame(const QImage &grayscale)
{
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = grayscale;
    }

    // present() may be called from the backend's own thread, so the repaint and the decode
    // are handed to the GUI thread rather than run here. Queued and not blocking: a frame
    // arriving while the last one is still being decoded is simply the next frame.
    QMetaObject::invokeMethod(this, "handleFrame", Qt::QueuedConnection);
}

void QrScanner::handleFrame()
{
    update();

    decode();
}

void QrScanner::decode()
{
    if (!m_quirc)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (now - m_lastDecodeMs < DecodeIntervalMs)
        return;

    m_lastDecodeMs = now;

    QImage frame;

    {
        QMutexLocker locker(&m_frameMutex);
        frame = m_frame;
    }

    if (frame.isNull())
        return;

    if (quirc_resize(m_quirc, frame.width(), frame.height()) < 0)
        return;

    int width = 0;
    int height = 0;
    uint8_t *buffer = quirc_begin(m_quirc, &width, &height);

    // quirc hands back a tightly packed w*h buffer, and QImage lines are padded to four
    // bytes, so this copies a line at a time rather than in one go.
    for (int y = 0; y < height; ++y)
        std::memcpy(buffer + static_cast<size_t>(y) * width, frame.constScanLine(y), width);

    quirc_end(m_quirc);

    const int found = quirc_count(m_quirc);

    for (int i = 0; i < found; ++i)
    {
        quirc_code code;
        quirc_data data;

        quirc_extract(m_quirc, i, &code);

        // A code that is there but not readable yet is the ordinary case while the camera
        // is focusing, so a failure here is not worth reporting - the next frame is.
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
            continue;

        const auto text = QString::fromUtf8(reinterpret_cast<const char *>(data.payload), data.payload_len);

        if (!text.isEmpty())
        {
            emit scanned(text);
            return;
        }
    }
}

int QrScanner::frameRotation() const noexcept
{
    return m_frameRotation;
}

void QrScanner::setFrameRotation(int degrees)
{
    // Normalised so a page can say -90 and mean the same as 270.
    const int wrapped = ((degrees % 360) + 360) % 360;

    if (m_frameRotation == wrapped)
        return;

    m_frameRotation = wrapped;

    emit frameRotationChanged();

    update();
}

void QrScanner::handleCameraError()
{
#ifdef MEEGRAM_QR_SCANNER
    if (m_camera)
        emit failed(m_camera->errorString());
#endif
}

void QrScanner::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    QImage frame;

    {
        QMutexLocker locker(&m_frameMutex);
        frame = m_frame;
    }

    if (frame.isNull())
    {
        painter->fillRect(boundingRect(), Qt::black);
        return;
    }

    painter->fillRect(boundingRect(), Qt::black);

    // Turned a quarter before it is drawn, because the sensor is mounted landscape and
    // hands over landscape frames whichever way the phone is held. Rotating the painter
    // rather than the QImage: transforming 640x480 per frame on this device is a copy
    // nobody needs, and the raster engine takes the transform for free.
    const bool quarterTurn = m_frameRotation % 180 != 0;

    // In the rotated frame of reference the item's width and height swap over, so the box
    // the picture has to fit inside does too.
    const QSize box = quarterTurn ? QSize(static_cast<int>(height()), static_cast<int>(width()))
                                  : QSize(static_cast<int>(width()), static_cast<int>(height()));

    // QSize::scale, not QSize::scaled: the const one arrived in Qt 5 and this is 4.7.
    QSize scaled = frame.size();
    scaled.scale(box, Qt::KeepAspectRatio);

    painter->save();

    // Nearest-neighbour. This is a viewfinder for aiming at a printed square, and a
    // smooth-scaled rotation per frame is real work on an SGX-less raster path.
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);

    painter->translate(width() / 2, height() / 2);
    painter->rotate(m_frameRotation);

    painter->drawImage(QRect(QPoint(-scaled.width() / 2, -scaled.height() / 2), scaled), frame);

    painter->restore();
}
