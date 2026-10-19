#include "ListRow.hpp"

#include "ChatPhotoProvider.hpp"
#include "File.hpp"
#include "StickerProvider.hpp"

#include <qdrawutil.h>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDeclarativeEngine>
#include <QDeclarativeImageProvider>
#include <QGraphicsSceneMouseEvent>
#include <QImageReader>
#include <QPainter>
#include <QRunnable>
#include <QTextBlock>

namespace rows {

namespace {

    // How long a press is held before it counts as a hold - MouseArea's pressAndHold delay.
    constexpr int HoldDelayMs = 800;

    QImage decode(ImageLoader::Kind kind, const QString &path, const QSize &size)
    {
        switch (kind)
        {
            case ImageLoader::Avatar: {
                static ChatPhotoProvider provider;
                return provider.requestImage(path, nullptr, size);
            }
            case ImageLoader::Webp: {
                static StickerProvider provider;
                return provider.requestImage(path, nullptr, size);
            }
            case ImageLoader::Fit:
            case ImageLoader::Crop:
                break;
        }

        // Decode at display size, the way sourceSize made QML's Image do it. The N9 has no
        // memory for a full resolution photo it would only scale down.
        QImageReader reader(path);
        const QSize source = reader.size();

        if (source.isValid() && !source.isEmpty() && size.isValid() && !size.isEmpty())
        {
            QSize target = source;
            target.scale(size, kind == ImageLoader::Crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);

            // Never up: a small image is drawn scaled instead, which costs nothing here.
            if (target.width() < source.width())
                reader.setScaledSize(target);
        }

        QImage image = reader.read();

        if (kind == ImageLoader::Crop && !image.isNull() && (image.width() > size.width() || image.height() > size.height()))
        {
            const int width = qMin(image.width(), size.width());
            const int height = qMin(image.height(), size.height());

            image = image.copy((image.width() - width) / 2, (image.height() - height) / 2, width, height);
        }

        return image;
    }

    class DecodeJob : public QRunnable
    {
    public:
        DecodeJob(ImageLoader *loader, const QString &key, ImageLoader::Kind kind, const QString &path, const QSize &size)
            : m_loader(loader)
            , m_key(key)
            , m_kind(kind)
            , m_path(path)
            , m_size(size)
        {
        }

        void run() override
        {
            const QImage image = decode(m_kind, m_path, m_size);

            QMetaObject::invokeMethod(m_loader, "finished", Qt::QueuedConnection, Q_ARG(QString, m_key), Q_ARG(QImage, image));
        }

    private:
        ImageLoader *m_loader;
        QString m_key;
        ImageLoader::Kind m_kind;
        QString m_path;
        QSize m_size;
    };

    // The document behind a rich TextBlock. QTextDocument loads every <img> itself, once per
    // document, so each bubble would decode its own copy of every emoji PNG it shows - QML's
    // Text shared them through its pixmap cache, and this is that cache.
    class EmojiDocument : public QTextDocument
    {
    protected:
        QVariant loadResource(int type, const QUrl &name) override
        {
            if (type != QTextDocument::ImageResource)
                return QTextDocument::loadResource(type, name);

            // Utils::emojiTag writes ":/emoji/<file>". Whether QUrl took that leading colon for
            // an empty scheme or kept it in the path, and for a qrc: url too, the file is
            // the path's last segment.
            const QString path = name.path();
            const QPixmap pixmap = emojiPixmap(path.mid(path.lastIndexOf(QLatin1Char('/')) + 1));

            if (pixmap.isNull())
                return QTextDocument::loadResource(type, name);

            return pixmap;
        }
    };

}  // namespace

ImageLoader *ImageLoader::instance()
{
    // Parented to the application, so the pool is drained before Qt goes away.
    static auto *loader = new ImageLoader(qApp);
    return loader;
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
    , m_cache(8192)
{
    // One reader thread, like QML's. The N9 has one core, so a second decoder would only
    // take turns with the first and with the frames being drawn.
    m_pool.setMaxThreadCount(1);
}

QString ImageLoader::key(Kind kind, const QString &path, const QSize &size)
{
    // The multi-argument arg(), so a '%' in a path cannot be taken for a placeholder.
    return QString::fromLatin1("%1|%2|%3x%4").arg(QString::number(kind), path, QString::number(size.width()), QString::number(size.height()));
}

QImage ImageLoader::fetch(const QString &key, Kind kind, const QString &path, const QSize &size)
{
    if (const auto *hit = m_cache.object(key))
        return *hit;

    if (!m_pending.contains(key))
    {
        m_pending.insert(key);
        m_pool.start(new DecodeJob(this, key, kind, path, size));
    }

    return QImage();
}

QImage ImageLoader::cached(const QString &key) const
{
    const auto *hit = m_cache.object(key);
    return hit ? *hit : QImage();
}

void ImageLoader::finished(const QString &key, const QImage &image)
{
    m_pending.remove(key);

    if (!image.isNull())
        m_cache.insert(key, new QImage(image), qMax(1, image.byteCount() / 1024));

    // Also on failure, so a row stops waiting and draws its placeholder.
    emit ready(key);
}

void RowImage::load(ImageLoader::Kind kind, const QString &path, const QSize &size)
{
    const QString wanted = ImageLoader::key(kind, path, size);

    if (wanted == key && !image.isNull())
        return;

    key = wanted;
    image = ImageLoader::instance()->fetch(key, kind, path, size);
}

bool RowImage::take(const QString &readyKey)
{
    if (readyKey != key)
        return false;

    image = ImageLoader::instance()->cached(key);
    return true;
}

void TextBlock::set(const QString &text, const QFont &font, bool rich, qreal width, Qt::Alignment align, bool wrap, Qt::TextElideMode elide)
{
    clear();

    if (text.isEmpty() || width <= 0)
        return;

    m_empty = false;

    QTextOption option(align);
    option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);

    if (rich)
    {
        m_document.reset(new EmojiDocument);
        m_document->setDocumentMargin(0);
        m_document->setDefaultFont(font);
        m_document->setDefaultTextOption(option);
        m_document->setHtml(text);
        m_document->setTextWidth(width);

        m_paintedWidth = qMin(m_document->idealWidth(), width);
        m_height = m_document->size().height();

        const QTextLayout *first = m_document->begin().layout();
        if (first && first->lineCount() > 0)
            m_ascent = first->lineAt(0).ascent();

        return;
    }

    // One line: cut here, the way a Label's elide does. Rich single lines arrive already
    // cut by Utils::elideEmoji, which knows how wide an emoji image is.
    const QString shown = wrap ? text : QFontMetricsF(font).elidedText(text, elide, width);

    m_layout.reset(new QTextLayout(shown, font));
    m_layout->setTextOption(option);
    m_layout->setCacheEnabled(true);
    m_layout->beginLayout();

    qreal y = 0;

    for (;;)
    {
        QTextLine line = m_layout->createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));

        if (y == 0)
            m_ascent = line.ascent();

        y += line.height();
        m_paintedWidth = qMax(m_paintedWidth, qMin(line.naturalTextWidth(), width));
    }

    m_layout->endLayout();
    m_height = y;
}

void TextBlock::clear()
{
    m_layout.reset();
    m_document.reset();
    m_paintedWidth = 0;
    m_height = 0;
    m_ascent = 0;
    m_empty = true;
}

void TextBlock::draw(QPainter *painter, const QPointF &topLeft, const QColor &color) const
{
    if (m_document)
    {
        painter->save();
        painter->translate(topLeft);

        // Links keep the application palette's colour, as they did in a QML Text.
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, color);

        m_document->documentLayout()->draw(painter, context);
        painter->restore();
    }
    else if (m_layout)
    {
        painter->setPen(color);
        m_layout->draw(painter, topLeft);
    }
}

QString TextBlock::anchorAt(const QPointF &point) const
{
    return m_document ? m_document->documentLayout()->anchorAt(point) : QString();
}

bool needsRichText(const QString &text) noexcept
{
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);

        switch (ch.unicode())
        {
            case '<':
            case '&':
            case '\n':
            case '\r':
            case '\t':
                return true;
            default:
                break;
        }

        if (ch.isSpace() && i + 1 < text.size() && text.at(i + 1).isSpace())
            return true;
    }

    return false;
}

const Palette &Palette::get(bool inverted) noexcept
{
    static const Palette light{QColor(0x19, 0x19, 0x19), QColor(0x50, 0x50, 0x50), QColor(0xcc, 0xcc, 0xcc), QColor(0x00, 0x77, 0xa8),
                               QColor(0x00, 0x00, 0x00), QColor(0x50, 0x50, 0x50), QColor(0x00, 0x77, 0xa8)};
    static const Palette dark{QColor(0xff, 0xff, 0xff), QColor(0x8c, 0x8c, 0x8c), QColor(0x3c, 0x3c, 0x3c), QColor(0x4f, 0xc3, 0xe8),
                              QColor(0xff, 0xff, 0xff), QColor(0x9a, 0x9a, 0x9a), QColor(0x4f, 0xc3, 0xe8)};

    return inverted ? dark : light;
}

QFont labelFont(int pixelSize, bool bold, bool light)
{
    QFont font(QLatin1String("Nokia Pure Text"));
    font.setPixelSize(pixelSize);

    if (bold)
        font.setBold(true);
    else if (light)
        font.setWeight(QFont::Light);

    return font;
}

QFont iconFont(int pixelSize)
{
    QFont font(QLatin1String("fontello"));
    font.setPixelSize(pixelSize);
    return font;
}

QPixmap themePixmap(const QObject *item, const QString &id)
{
    // Failures are cached too: a wrong name renders nothing, the same as a QML Image
    // given one, and asking again every frame would not change that.
    static QHash<QString, QPixmap> cache;

    if (const auto it = cache.constFind(id); it != cache.constEnd())
        return it.value();

    QPixmap pixmap;

    if (const auto *engine = qmlEngine(item))
    {
        if (auto *provider = engine->imageProvider(QLatin1String("theme")))
        {
            QSize size;

            pixmap = provider->imageType() == QDeclarativeImageProvider::Pixmap ? provider->requestPixmap(id, &size, QSize())
                                                                                : QPixmap::fromImage(provider->requestImage(id, &size, QSize()));
        }
    }

    if (pixmap.isNull())
        qWarning() << "ListRow: no theme graphic" << id;

    cache.insert(id, pixmap);

    return pixmap;
}

QPixmap spinnerFrame(const QObject *item, bool inverted, int size)
{
    // BusyIndicatorStyle: "spinner" or "spinnerinverted", the size, then frames 1 to 10
    // across a 1000 ms period.
    const int frame = static_cast<int>((QDateTime::currentMSecsSinceEpoch() / 100) % 10) + 1;

    return themePixmap(
        item,
        QString::fromLatin1("spinner%1_%2_%3").arg(inverted ? QLatin1String("inverted") : QLatin1String(""), QString::number(size), QString::number(frame)));
}

void drawBorderImage(QPainter *painter, const QRectF &target, const QPixmap &pixmap, int border)
{
    if (!pixmap.isNull())
        qDrawBorderPixmap(painter, target.toRect(), QMargins(border, border, border, border), pixmap);
}

void drawFitted(QPainter *painter, const QRectF &target, const QImage &image)
{
    if (image.isNull())
        return;

    QSizeF size = image.size();
    size.scale(target.size(), Qt::KeepAspectRatio);

    QRectF rect(QPointF(), size);
    rect.moveCenter(target.center());

    painter->drawImage(rect, image);
}

void drawCropped(QPainter *painter, const QRectF &target, const QImage &image)
{
    if (image.isNull())
        return;

    QSizeF size = target.size();
    size.scale(image.size(), Qt::KeepAspectRatio);

    const QRectF source(QPointF((image.width() - size.width()) / 2, (image.height() - size.height()) / 2), size);

    painter->drawImage(target, image, source);
}

QPixmap emojiPixmap(const QString &file)
{
    // ponytail: 1024 glyphs at 32x32 is 4 MB, which holds the picker's largest tab with room
    // for a chat's worth besides. Raise it if switching tabs shows decodes coming back.
    static QCache<QString, QPixmap> cache(1024);

    if (const auto *cached = cache.object(file))
        return *cached;

    const QPixmap pixmap(QLatin1String(":/emoji/") + file);

    if (!pixmap.isNull())
        cache.insert(file, new QPixmap(pixmap));

    return pixmap;
}

PressItem::PressItem(QDeclarativeItem *parent)
    : QDeclarativeItem(parent)
{
    setFlag(QGraphicsItem::ItemHasNoContents, false);
    // QDeclarativeItem takes no buttons by default; a MouseArea is what used to.
    setAcceptedMouseButtons(Qt::LeftButton);
}

QObject *PressItem::actions() const noexcept
{
    return m_actions;
}

void PressItem::setActions(QObject *actions)
{
    m_actions = actions;
}

bool PressItem::inverted() const noexcept
{
    return m_inverted;
}

void PressItem::setInverted(bool inverted)
{
    if (inverted == m_inverted)
        return;

    m_inverted = inverted;
    update();
}

void PressItem::held(int region, const QPointF &point)
{
    Q_UNUSED(region);
    Q_UNUSED(point);
}

void PressItem::pressEnded(bool cancelled)
{
    Q_UNUSED(cancelled);
}

QVariant PressItem::act(const char *function, const QVariantList &arguments) const
{
    if (!m_actions)
        return QVariant();

    QGenericArgument values[7];

    for (int i = 0; i < arguments.size() && i < 7; ++i)
        values[i] = Q_ARG(QVariant, arguments.at(i));

    QVariant result;

    // A QML function's parameters are all QVariant, and the call only resolves at exactly
    // the arity it was declared with - which is why every caller passes every argument.
    if (!QMetaObject::invokeMethod(m_actions, function, Q_RETURN_ARG(QVariant, result), values[0], values[1], values[2], values[3], values[4], values[5],
                                   values[6]))
        qWarning() << metaObject()->className() << ": the actions object has no" << function << "taking" << arguments.size() << "arguments";

    return result;
}

void PressItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    const int region = hitTest(event->pos());

    // Nothing here takes it, so the list gets it - the gap beside a bubble flicks.
    if (region == 0)
    {
        event->ignore();
        return;
    }

    m_pressRegion = region;
    m_pressPos = event->pos();
    m_held = false;
    m_inside = true;
    m_dragging = false;
    m_holdTimer.start(HoldDelayMs, this);

    update();
}

void PressItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    m_inside = boundingRect().contains(event->pos());

    if (!m_inside)
        stopHold();
}

void PressItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);

    if (m_pressRegion == 0)
        return;

    const int region = m_pressRegion;
    const QPointF point = m_pressPos;
    const bool click = !m_held && !m_dragging && m_inside;

    endPress(false);

    // Last: what a click opens can rebuild the list, and this item with it.
    if (click)
        clicked(region, point);
}

void PressItem::timerEvent(QTimerEvent *event)
{
    if (event->timerId() != m_holdTimer.timerId())
    {
        QDeclarativeItem::timerEvent(event);
        return;
    }

    m_holdTimer.stop();

    if (m_pressRegion == 0 || m_dragging || !m_inside)
        return;

    m_held = true;
    held(m_pressRegion, m_pressPos);
}

bool PressItem::sceneEvent(QEvent *event)
{
    // The list taking the grab for a flick, or a menu taking it on a hold. A release
    // ungrabs too, after the press has already been ended above.
    if (event->type() == QEvent::UngrabMouse && m_pressRegion != 0)
        endPress(true);

    return QDeclarativeItem::sceneEvent(event);
}

void PressItem::stopHold()
{
    m_holdTimer.stop();
}

void PressItem::endPress(bool cancelled)
{
    m_holdTimer.stop();
    m_pressRegion = 0;
    m_held = false;

    pressEnded(cancelled);

    m_dragging = false;
    update();
}

ListRow::ListRow(QDeclarativeItem *parent)
    : PressItem(parent)
{
    m_spinTimer.setInterval(100);
    connect(&m_spinTimer, SIGNAL(timeout()), this, SLOT(spin()));

    connect(ImageLoader::instance(), SIGNAL(ready(QString)), this, SLOT(handleImageReady(QString)));
}

QObject *ListRow::listModel() const noexcept
{
    return m_model;
}

void ListRow::setListModel(QObject *model)
{
    auto *itemModel = qobject_cast<QAbstractItemModel *>(model);

    if (itemModel == m_model)
        return;

    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);

    m_model = itemModel;

    if (m_model)
    {
        connect(m_model, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(handleDataChanged(QModelIndex, QModelIndex)));
        connect(m_model, SIGNAL(layoutChanged()), this, SLOT(handleLayoutChanged()));
        connect(m_model, SIGNAL(modelReset()), this, SLOT(handleLayoutChanged()));
    }

    reloadIfComplete();
}

int ListRow::row() const noexcept
{
    return m_row;
}

void ListRow::setRow(int row)
{
    if (row == m_row)
        return;

    m_row = row;

    // Painting can read the number itself - MessageRow's separator skips row 0.
    update();

    // Removed rows are handed -1 on their way out; keep drawing what they were.
    if (!isComponentComplete() || !m_model || row < 0 || row >= m_model->rowCount())
        return;

    if (m_key != 0 && keyAt(row) == m_key)
        return;

    reloadIfComplete();
}

QVariant ListRow::data(int role) const
{
    return m_model ? m_model->data(m_model->index(m_row, 0), role) : QVariant();
}

void ListRow::watch(File *file)
{
    if (!file)
        return;

    m_files.append(file);
    connect(file, SIGNAL(fileChanged()), this, SLOT(handleFileChanged()), Qt::UniqueConnection);
    // Progress has a signal of its own so fileChanged can stay narrow - see File.hpp. A
    // row drawing a download ring wants both, and only a row bound to a file that is
    // actually downloading is ever told twice.
    connect(file, SIGNAL(progressChanged()), this, SLOT(handleFileChanged()), Qt::UniqueConnection);
}

void ListRow::unwatchFiles()
{
    for (const auto &file : m_files)
    {
        if (file)
            disconnect(file, nullptr, this, nullptr);
    }

    m_files.clear();
}

void ListRow::downloadOnce(File *file)
{
    if (!file || !file->canBeDownloaded() || file->isDownloadingActive() || file->isDownloadingCompleted() || m_requested.contains(file->id()))
        return;

    m_requested.insert(file->id());
    act("download", QVariantList() << file->id());
}

void ListRow::setSpinning(bool spinning)
{
    if (spinning && !m_spinTimer.isActive())
        m_spinTimer.start();
    else if (!spinning)
        m_spinTimer.stop();
}

void ListRow::componentComplete()
{
    QDeclarativeItem::componentComplete();
    reloadIfComplete();
}

void ListRow::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QDeclarativeItem::geometryChanged(newGeometry, oldGeometry);

    // Only the width feeds the layout. The height is the layout's own answer, and the
    // list moving a row changes neither.
    if (newGeometry.width() != oldGeometry.width())
        reloadIfComplete();
}

void ListRow::handleDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (m_row >= topLeft.row() && m_row <= bottomRight.row())
        reloadIfComplete();
}

void ListRow::handleLayoutChanged()
{
    reloadIfComplete();
}

void ListRow::handleFileChanged()
{
    reloadIfComplete();
}

void ListRow::handleImageReady(const QString &key)
{
    imageReady(key);
}

void ListRow::spin()
{
    // Rows in the cache buffer spin too, and with a full-viewport update mode every
    // update() repaints the whole screen - so only ask when this row can be seen. The
    // view is the ListView: the row's parent is its contentItem.
    const QGraphicsItem *view = parentItem() ? parentItem()->parentItem() : nullptr;

    if (view && !view->boundingRect().intersects(view->mapRectFromItem(this, boundingRect())))
        return;

    update();
}

void ListRow::reloadIfComplete()
{
    if (!isComponentComplete() || !m_model || m_row < 0 || m_row >= m_model->rowCount())
        return;

    const qlonglong key = keyAt(m_row);

    if (key != m_key)
        m_requested.clear();

    m_key = key;
    reload();
}

}  // namespace rows
