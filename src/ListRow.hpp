#pragma once

#include <QAbstractItemModel>
#include <QBasicTimer>
#include <QCache>
#include <QDeclarativeItem>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QTextDocument>
#include <QTextLayout>
#include <QThreadPool>
#include <QTimer>

#include <memory>

class File;

// Shared plumbing for ChatRow and MessageRow, the two list delegates that used to be
// ChatItem.qml and MessageDelegate.qml + MessageBubble.qml. A QML delegate cost a dozen
// items and a few dozen bindings per chat row, and around thirty items, a Loader and two
// Repeaters per message - all of it built again every time a row scrolled into the cache
// buffer. A row here is one item that reads its model row in C++, lays its text out
// once, and paints.
//
// The ListViews themselves stay in QML: flicking, sections, positionViewAtIndex and
// ChatPage's settle logic all keep working unchanged.

namespace rows {

// Off-GUI-thread image decoding for the rows, with a byte-budget cache in front. This
// is what QML's asynchronous Image did for them: one reader thread, results handed back
// on the GUI thread. Avatars and webp still go through ChatPhotoProvider and
// StickerProvider, so their own caches (and the avatar disk cache) keep working.
class ImageLoader : public QObject
{
    Q_OBJECT

public:
    enum Kind {
        // Cropped, masked avatar from ChatPhotoProvider.
        Avatar,
        // Scaled to fit inside the size, never up.
        Fit,
        // Scaled to cover the size and cut to it, for album cells.
        Crop,
        // libwebp through StickerProvider: stickers and webp video stills.
        Webp
    };

    static ImageLoader *instance();

    static QString key(Kind kind, const QString &path, const QSize &size);

    // The decoded image, or a null one while it is being decoded - ready(key) follows.
    QImage fetch(const QString &key, Kind kind, const QString &path, const QSize &size);

    // Cache only; never starts a decode.
    QImage cached(const QString &key) const;

signals:
    void ready(const QString &key);

private slots:
    void finished(const QString &key, const QImage &image);

private:
    explicit ImageLoader(QObject *parent);

    QThreadPool m_pool;

    // Cost in KB. ponytail: one 8 MB budget for every kind, sized for a screenful of
    // photos either side; split per kind if avatars get evicted by photos.
    QCache<QString, QImage> m_cache;
    QSet<QString> m_pending;
};

// An image a row draws, with the key it is waiting on.
struct RowImage
{
    QString key;
    QImage image;

    void clear()
    {
        key.clear();
        image = QImage();
    }

    // Asks for the image, keeping the one already held when nothing changed - a row
    // re-read for its reactions must not drop its photo and flash the scrim.
    void load(ImageLoader::Kind kind, const QString &path, const QSize &size);

    // Takes a finished decode if it is this one's. True when the row should repaint.
    bool take(const QString &readyKey);
};

// One label's worth of text, laid out once. Plain text goes straight to a QTextLayout;
// anything with markup, an escaped entity, a line break or whitespace HTML would
// collapse goes through a QTextDocument - the same split the QML labels made with their
// textFormat test, and for the same reason: most messages are plain, and a document
// per bubble was the expensive part.
//
// The text is laid out in a box `width` wide and drawn from the box's top-left, so the
// alignment is the box's and callers never offset by the painted width themselves.
class TextBlock
{
public:
    void set(const QString &text, const QFont &font, bool rich, qreal width, Qt::Alignment align = Qt::AlignLeft, bool wrap = true,
             Qt::TextElideMode elide = Qt::ElideRight);
    void clear();

    bool isEmpty() const noexcept
    {
        return m_empty;
    }

    // What the text actually covers, which is what the bubbles size themselves from.
    qreal paintedWidth() const noexcept
    {
        return m_paintedWidth;
    }
    qreal height() const noexcept
    {
        return m_height;
    }
    qreal ascent() const noexcept
    {
        return m_ascent;
    }

    void draw(QPainter *painter, const QPointF &topLeft, const QColor &color) const;

    // The link under a point relative to the box, or empty.
    QString anchorAt(const QPointF &point) const;

private:
    std::unique_ptr<QTextLayout> m_layout;
    std::unique_ptr<QTextDocument> m_document;

    qreal m_paintedWidth{};
    qreal m_height{};
    qreal m_ascent{};
    bool m_empty{true};
};

// What the QML labels' textFormat test said: /[<&\n\r\t]|\s\s/. Rich if it has markup or
// an entity, and also if it has whitespace HTML would collapse, because those are the
// cases where the two formats draw different things.
bool needsRichText(const QString &text) noexcept;

// The colours main.qml gives appWindow, for the two things that paint them from C++.
// Keep the two in step.
struct Palette
{
    QColor text;  // a Label's own colour, LabelStyle's textColor
    QColor secondary;
    QColor separator;
    QColor accent;
    QColor bubbleText;
    QColor bubbleSecondary;
    QColor bubbleAccent;

    static const Palette &get(bool inverted) noexcept;
};

// Puts the painter back after a row paints: the render hints, opacity and fonts one row
// sets would otherwise carry over to whatever the view paints next.
class PainterGuard
{
public:
    explicit PainterGuard(QPainter *painter)
        : m_painter(painter)
    {
        m_painter->save();
    }
    ~PainterGuard()
    {
        m_painter->restore();
    }

private:
    QPainter *m_painter;
};

// "Nokia Pure Text" at a pixel size, as a Label would set it.
QFont labelFont(int pixelSize, bool bold = false, bool light = false);

// The fontello glyph font from Icons.qml.
QFont iconFont(int pixelSize);

// A theme graphic, the image://theme/<id> a QML Image would load, through the engine's
// own provider. Cached for the process; the theme does not change under a running app.
QPixmap themePixmap(const QObject *item, const QString &id);

// The platform BusyIndicator's current frame: the theme's ten spinner images, one second
// a turn, so every spinner on screen turns in step the way theirs did.
QPixmap spinnerFrame(const QObject *item, bool inverted, int size);

// BorderImage: a nine-slice with the same margin on all four sides.
void drawBorderImage(QPainter *painter, const QRectF &target, const QPixmap &pixmap, int border);

// Image fillMode PreserveAspectFit / PreserveAspectCrop into a rectangle.
void drawFitted(QPainter *painter, const QRectF &target, const QImage &image);
void drawCropped(QPainter *painter, const QRectF &target, const QImage &image);

// An emoji asset (":/emoji/<file>") at its native 32px, decoded once. Shared by the rich
// text in the rows and by the emoji picker, so a glyph is decoded once for both.
QPixmap emojiPixmap(const QString &file);

// An item that turns mouse input into press / hold / click the way a MouseArea did, and
// carries out what a tap asks for through a QML actions object.
class PressItem : public QDeclarativeItem
{
    Q_OBJECT

    // A QML object whose functions carry out what a tap asks for - opening a chat, a menu,
    // a photo. The one seam between a painted item and the page it is on; each subclass
    // lists the functions it calls.
    Q_PROPERTY(QObject *actions READ actions WRITE setActions)

    // theme.inverted.
    Q_PROPERTY(bool inverted READ inverted WRITE setInverted)

public:
    explicit PressItem(QDeclarativeItem *parent = nullptr);

    QObject *actions() const noexcept;
    void setActions(QObject *actions);

    bool inverted() const noexcept;
    void setInverted(bool inverted);

protected:
    // Mouse input, in item coordinates. hitTest picks what a press landed on; 0 means
    // nothing, and the press goes through to whatever is underneath - the list it sits in.
    virtual int hitTest(const QPointF &point) const = 0;
    virtual void clicked(int region, const QPointF &point) = 0;
    // A hold always swallows the click that would follow it, as on a MouseArea, whether
    // or not anything is done with it.
    virtual void held(int region, const QPointF &point);
    // A press that ended without becoming a click: released after a hold or a drag, or
    // taken by the list.
    virtual void pressEnded(bool cancelled);

    // Calls a function on the actions object with up to seven arguments.
    QVariant act(const char *function, const QVariantList &arguments = QVariantList()) const;

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    bool sceneEvent(QEvent *event) override;

    // A hold that has not fired yet stops waiting - a drag has started.
    void stopHold();

    QPointer<QObject> m_actions;
    bool m_inverted{false};

    // The press in progress: what it landed on, where, and what it has turned into. A
    // press that wanders off the item, is held or is dragged does not click.
    int m_pressRegion{0};
    QPointF m_pressPos;
    bool m_held{false};
    bool m_inside{false};
    bool m_dragging{false};

private:
    void endPress(bool cancelled);

    QBasicTimer m_holdTimer;
};

// Base delegate: binds to one row of a model and re-reads it when the model says so.
// Subclasses lay out in reload() and paint.
class ListRow : public PressItem
{
    Q_OBJECT

    // The view's model and this delegate's `index`.
    Q_PROPERTY(QObject *listModel READ listModel WRITE setListModel)
    Q_PROPERTY(int row READ row WRITE setRow)

public:
    explicit ListRow(QDeclarativeItem *parent = nullptr);

    QObject *listModel() const noexcept;
    void setListModel(QObject *model);

    int row() const noexcept;
    void setRow(int row);

protected:
    // Reads the row from the model and lays it out. Called once the item is complete,
    // and again whenever the model reports this row changed or its width moved.
    virtual void reload() = 0;

    // The id at a row, which decides whether a new `index` is a new row or the same one
    // renumbered by an insert above it. Prepending a page of history renumbers every
    // delegate, and re-laying all of them out for nothing was the spike to avoid.
    virtual qlonglong keyAt(int row) const = 0;

    // Ready decodes from ImageLoader; repaint when one is this row's.
    virtual void imageReady(const QString &key) = 0;

    QVariant data(int role) const;

    // Rows draw download state, so a change on any of these files re-reads the row.
    void watch(File *file);
    void unwatchFiles();

    // Asks TDLib for a file once per row: delegates fetch on sight, and re-reading the
    // row on every fileChanged would otherwise re-request a download that failed.
    void downloadOnce(File *file);

    // Repaints every 100 ms while something on the row is spinning.
    void setSpinning(bool spinning);

    void componentComplete() override;
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

    // reload(), once the item is complete and the row is a real one.
    void reloadIfComplete();

    QPointer<QAbstractItemModel> m_model;
    int m_row{-1};
    qlonglong m_key{0};

private slots:
    void handleDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void handleLayoutChanged();
    void handleFileChanged();
    void handleImageReady(const QString &key);
    void spin();

private:
    QList<QPointer<File>> m_files;
    QSet<int> m_requested;

    QTimer m_spinTimer;
};

}  // namespace rows
