#pragma once

#include "ListRow.hpp"

#include <QPointer>

#include <vector>

class File;
class LottieAnimation;
class VoiceNote;
class QPropertyAnimation;

// One message in ChatPage's list, painted - what MessageDelegate.qml, MessageBubble.qml
// and ServiceMessageDelegate.qml used to build. Reads a MessageModel row. The geometry
// is theirs, number for number; the reasons behind each number are in docs/ and in the
// git history of those files.
//
// Calls on `actions`: openMenu(messageId, sender, text, outgoing, file, fileName, photos),
// reply(messageId, sender, preview), goToMessage(messageId), openLink(link),
// openPhoto(path, original), openProfile(userId), download(fileId),
// cancelDownload(fileId), showBanner(text), isDownloadCancelled(fileId),
// setDownloadCancelled(fileId, cancelled).
class MessageRow : public rows::ListRow
{
    Q_OBJECT

    // appWindow.showBubbles, skeuomorphicBubbles and animateStickers.
    Q_PROPERTY(bool bubbles READ bubbles WRITE setBubbles)
    Q_PROPERTY(bool skeuomorphic READ skeuomorphic WRITE setSkeuomorphic)
    Q_PROPERTY(bool animateStickers READ animateStickers WRITE setAnimateStickers)

    // ChatPage's VoiceNote: one player for every bubble, so starting a note stops the
    // one already playing.
    Q_PROPERTY(QObject *player READ player WRITE setPlayer)

    // The message a quote tap jumped to, and the list's one shared flash animation.
    Q_PROPERTY(QString flashMessageId READ flashMessageId WRITE setFlashMessageId)
    //
    // double, not qreal: Qt 4's moc writes a qreal property's type as QMetaType::QReal << 24,
    // which on ARM (qreal is float, 135) lands in the sign bit, and GCC 14 rejects the
    // narrowing into moc's uint table.
    Q_PROPERTY(double flashOpacity READ flashOpacity WRITE setFlashOpacity)

public:
    explicit MessageRow(QDeclarativeItem *parent = nullptr);

    bool bubbles() const noexcept;
    void setBubbles(bool bubbles);

    bool skeuomorphic() const noexcept;
    void setSkeuomorphic(bool skeuomorphic);

    bool animateStickers() const noexcept;
    void setAnimateStickers(bool animate);

    QObject *player() const noexcept;
    void setPlayer(QObject *player);

    QString flashMessageId() const;
    void setFlashMessageId(const QString &messageId);

    double flashOpacity() const noexcept;
    void setFlashOpacity(double opacity);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    void reload() override;
    qlonglong keyAt(int row) const override;
    int hitTest(const QPointF &point) const override;
    void clicked(int region, const QPointF &point) override;
    void held(int region, const QPointF &point) override;
    void pressEnded(bool cancelled) override;
    void imageReady(const QString &key) override;

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

private slots:
    void playerChanged();
    void startLottie();

private:
    enum class Kind { Hidden, Service, Text, Photo, Album, Sticker, Video, Document, Voice, Unsupported };

    struct Pill
    {
        QRectF rect;
        QString emoji;
        QString icon;
        QString count;
        bool chosen{false};
    };

    struct Cell
    {
        QRectF rect;
        QPointer<File> file;
        QPointer<File> original;
        rows::RowImage image;
    };

    // Each content type lays itself out below the sender and the quote, from `top`, and
    // answers its height; m_childrenWidth is what the balloon wraps.
    qreal layoutContent(const QVariant &content, qreal top, qreal avatarSpace);
    qreal layoutCaption(const QString &caption, qreal x, qreal y, qreal width);
    // Document and voice rows: the round icon with a title and a line under it.
    qreal layoutFileRow(qreal x, qreal top, qreal width, const QString &title, const QString &subtitle);
    void layoutService();

    void showLottie(const QString &path);
    void dropLottie();

    // The finger's position in the list's coordinates. Not the scene's: Harmattan draws a
    // portrait app by rotating the window's content, so in the scene a sideways swipe is
    // vertical and a scroll is sideways. Not this row's either, which moves under the
    // finger while it is dragged. MouseArea measured its drag the same way, in its target's
    // parent.
    QPointF listPoint(const QGraphicsSceneMouseEvent *event) const;

    void openMenu(File *file, const QString &fileName = QString(), const QVariant &photos = QVariant());
    // A tap on a document, a video or a voice note: open it, stop it, or fetch it.
    void openOrFetch();
    void toggleVoice();

    bool flashing() const;
    bool isFileActive() const;
    bool isFileReady() const;
    QColor contentColor() const;

    void paintContent(QPainter *painter, const rows::Palette &palette) const;
    void paintSpinner(QPainter *painter, const QPointF &center) const;

    // The download indicator: the platform spinner until the first chunk lands, then a
    // determinate arc. The spinner alone looks the same at 2% as at 98%, which on a
    // phone downloading a 40 MB video over 3G is the whole complaint.
    void paintProgress(QPainter *painter, const QPointF &center) const;

    // The line under a document's name: its size, counting up while it downloads.
    QString fileSubtitle() const;

    bool m_bubbles{true};
    bool m_skeuomorphic{false};
    bool m_animateStickers{true};
    QPointer<VoiceNote> m_player;
    QString m_flashMessageId;
    qreal m_flashOpacity{1.0};

    Kind m_kind{Kind::Hidden};
    QString m_contentType;
    QString m_idString;
    QString m_sender;
    bool m_outgoing{false};
    bool m_opensRun{false};
    bool m_showAvatar{false};
    bool m_senderVisible{false};
    bool m_rankVisible{false};
    bool m_coloredSender{false};
    QColor m_senderColor;
    QString m_sendState;
    QString m_replyToMessageId;
    bool m_hasReply{false};

    // What the menu and a swipe-to-reply carry: the message's text or caption.
    QString m_text;

    QPointer<File> m_file;
    QPointer<File> m_original;
    QPointer<File> m_thumbnail;
    QPointer<File> m_senderPhoto;
    QString m_fileName;
    QVariant m_album;
    bool m_unplayed{false};
    bool m_spinning{false};

    rows::TextBlock m_senderText;
    rows::TextBlock m_rankText;
    rows::TextBlock m_replySenderText;
    rows::TextBlock m_replyText;
    rows::TextBlock m_body;
    rows::TextBlock m_caption;
    rows::TextBlock m_dateText;
    rows::TextBlock m_badgeText;
    rows::TextBlock m_nameText;
    rows::TextBlock m_subText;

    rows::RowImage m_avatar;
    rows::RowImage m_media;
    std::vector<Pill> m_pills;
    std::vector<Cell> m_cells;

    qreal m_childrenWidth{0};
    QRectF m_balloon;
    QRectF m_avatarRect;
    QPointF m_senderPos;
    QPointF m_rankPos;
    QRectF m_replyRect;
    qreal m_replyWashWidth{0};
    QPointF m_bodyPos;
    QPointF m_captionPos;
    QRectF m_frame;
    QRectF m_iconRect;
    QPointF m_namePos;
    QPointF m_subPos;
    QRectF m_dotRect;
    QRectF m_badgeRect;
    QRectF m_dateBox;
    qreal m_tickRight{0};

    QPointer<LottieAnimation> m_lottie;
    QString m_lottiePath;

    // Swipe to reply: the row follows the finger left and springs back.
    QPropertyAnimation *m_spring;
    qreal m_swipeStartX{0};
    // Where the press landed, in the list's coordinates - see listPoint().
    QPointF m_pressPoint;
    // Set once the finger has gone vertical: this press is a scroll and never a swipe.
    bool m_swipeRefused{false};
};
