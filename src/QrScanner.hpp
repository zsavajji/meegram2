#pragma once

#include <QDeclarativeItem>
#include <QImage>
#include <QMutex>

#ifdef MEEGRAM_QR_SCANNER
// No QTM_USE_NAMESPACE here, unlike the QtContacts block in SearchModel.cpp. Harmattan's
// QtMultimediaKit headers open QT_BEGIN_NAMESPACE, not QTM_BEGIN_NAMESPACE - verified in
// the sysroot - so these classes are in the ordinary Qt namespace and the using-directive
// would name a namespace that does not exist.
#include <QAbstractVideoSurface>
#include <QCamera>
#endif

struct quirc;

// A viewfinder that reads QR codes. Used for "link a desktop device": Telegram Desktop
// shows a QR holding a tg://login?token=... link, this scans it, and
// confirmQrCodeAuthentication signs that device in.
//
// The counterpart to QrCodeItem, which *draws* a QR for signing this device in from another
// one. Encoding and decoding are different libraries - lib/QR-Code-generator and lib/quirc -
// and neither does the other's half.
//
// The whole class is built without a camera when QtMultimediaKit is not in the sysroot, the
// same way the contacts import is optional (see CMakeLists.txt). `available` is then false
// and the page says so rather than showing a black rectangle.
class QrScanner : public QDeclarativeItem
{
    Q_OBJECT

    // Whether a camera is running. Set false when the page closes, which is what releases
    // the device - Harmattan has one camera and a viewfinder left running holds it against
    // every other application.
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

    // Whether this build and this device can scan at all.
    Q_PROPERTY(bool available READ isAvailable CONSTANT)

public:
    explicit QrScanner(QDeclarativeItem *parent = nullptr);
    ~QrScanner() override;

    bool isActive() const noexcept;
    void setActive(bool active);

    bool isAvailable() const noexcept;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    // A QR code was read. Carries its text exactly as it was encoded; the page decides
    // whether it is a login link and what to do with it.
    void scanned(const QString &text);

    void activeChanged();

    void failed(const QString &message);

private slots:
    // Queued from present(), which the camera backend may call on a thread of its own.
    void handleFrame();

    void handleCameraError();

private:
    friend class QrVideoSurface;

    // Called from present(), on whatever thread that is. Takes m_frameMutex.
    void takeFrame(const QImage &grayscale);

    // The decode itself, on the GUI thread, throttled - see DecodeIntervalMs.
    void decode();

    QImage m_frame;
    QMutex m_frameMutex;

    bool m_active{false};

    // When the last decode attempt ran, so a 30fps viewfinder does not run quirc 30 times a
    // second on a single core. Aiming a phone at a screen takes longer than this anyway.
    qint64 m_lastDecodeMs{0};

    quirc *m_quirc{nullptr};

#ifdef MEEGRAM_QR_SCANNER
    QCamera *m_camera{nullptr};
    QAbstractVideoSurface *m_surface{nullptr};
#endif
};
