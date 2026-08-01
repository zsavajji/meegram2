#include "MessageContent.hpp"

#include "Utils.hpp"

namespace {

// The N9 screen is 854x480 and a bubble is narrower still, so the 1280px original
// costs bandwidth and decode time for detail that is never drawn. Telegram ships
// several sizes per photo; take the smallest one that still covers the screen.
constexpr int PreferredPhotoWidth = 480;

// While nothing seen so far is big enough, keep taking the bigger one; once something
// is, only take a smaller one that is still big enough. Sizes arrive in no guaranteed
// order, so this has to hold either way round.
constexpr bool isBetterPhotoWidth(int candidate, int current, int preferred)
{
    return current < preferred ? candidate > current : candidate >= preferred && candidate < current;
}

static_assert(isBetterPhotoWidth(320, 90, 480));     // nothing covers it yet - grow
static_assert(isBetterPhotoWidth(800, 320, 480));    // still short - grow
static_assert(!isBetterPhotoWidth(1280, 800, 480));  // 800 already covers it - keep
static_assert(isBetterPhotoWidth(800, 1280, 480));   // covers it and is smaller - take
static_assert(!isBetterPhotoWidth(320, 800, 480));   // smaller, but too small

td::td_api::photoSize *pickPhotoSize(std::vector<td::td_api::object_ptr<td::td_api::photoSize>> &sizes)
{
    td::td_api::photoSize *best = nullptr;

    for (auto &size : sizes)
    {
        if (!size)
            continue;

        if (!best || isBetterPhotoWidth(size->width_, best->width_, PreferredPhotoWidth))
            best = size.get();
    }

    return best;
}

// Which thumbnail formats there is something here to draw with, as the lowercase string
// the delegate switches on - same vocabulary as MessageSticker::format. QML decodes jpeg
// and png itself; webp goes through StickerProvider and libwebp, which is a general webp
// decoder keyed by path and only called "sticker" because stickers got there first.
//
// Empty for anything else, which in practice means TDLib's animated mpeg4 preview: that
// is video, and not decoding video in-process is the whole point of these two bubbles.
// The caller leaves the file null on empty, so nothing is downloaded to draw nothing.
QString decodableThumbnailFormat(const td::td_api::thumbnail &thumbnail)
{
    if (!thumbnail.format_)
        return {};

    switch (thumbnail.format_->get_id())
    {
        case td::td_api::thumbnailFormatJpeg::ID:
            return QLatin1String("jpeg");
        case td::td_api::thumbnailFormatPng::ID:
            return QLatin1String("png");
        case td::td_api::thumbnailFormatWebp::ID:
            return QLatin1String("webp");
        default:
            return {};
    }
}

}  // namespace

MessageText::MessageText(td::td_api::object_ptr<td::td_api::messageText> content, QObject *parent)
    : QObject(parent)
{
    m_text = QString::fromStdString(content->text_->text_);
    m_formattedText = Utils::formattedText(content->text_);
}

QString MessageText::text() const
{
    return m_text;
}

QString MessageText::formattedText() const
{
    return m_formattedText;
}

MessageAnimation::MessageAnimation(td::td_api::object_ptr<td::td_api::messageAnimation> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    // Guarded the same way MessageDocument is: a message TDLib hands over without an
    // animation part is a bubble with no still, not a crash.
    if (!content->animation_)
        return;

    auto &animation = content->animation_;

    m_width = animation->width_;
    m_height = animation->height_;
    m_duration = animation->duration_;
    m_fileName = QString::fromStdString(animation->file_name_);

    if (animation->animation_)
        m_file = std::make_shared<File>(std::move(animation->animation_));

    if (animation->thumbnail_ && animation->thumbnail_->file_)
    {
        m_thumbnailFormat = decodableThumbnailFormat(*animation->thumbnail_);

        if (!m_thumbnailFormat.isEmpty())
            m_thumbnailFile = std::make_shared<File>(std::move(animation->thumbnail_->file_));
    }
}

QString MessageAnimation::caption() const
{
    return m_caption;
}

File *MessageAnimation::file() const
{
    return m_file.get();
}

File *MessageAnimation::thumbnailFile() const
{
    return m_thumbnailFile.get();
}

QString MessageAnimation::thumbnailFormat() const
{
    return m_thumbnailFormat;
}

int MessageAnimation::width() const
{
    return m_width;
}

int MessageAnimation::height() const
{
    return m_height;
}

int MessageAnimation::duration() const
{
    return m_duration;
}

QString MessageAnimation::fileName() const
{
    return m_fileName;
}

const std::shared_ptr<File> &MessageAnimation::animationFile() const noexcept
{
    return m_file;
}

void MessageAnimation::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

const std::shared_ptr<File> &MessageAnimation::animationThumbnailFile() const noexcept
{
    return m_thumbnailFile;
}

void MessageAnimation::adoptThumbnailFile(std::shared_ptr<File> file) noexcept
{
    m_thumbnailFile = std::move(file);
}

MessageAudio::MessageAudio(td::td_api::object_ptr<td::td_api::messageAudio> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    if (content->audio_)
    {
        m_duration = content->audio_->duration_;
        m_title = QString::fromStdString(content->audio_->title_);
        m_performer = QString::fromStdString(content->audio_->performer_);
        m_fileName = QString::fromStdString(content->audio_->file_name_);

        if (content->audio_->audio_)
            m_file = std::make_shared<File>(std::move(content->audio_->audio_));
    }
}

QString MessageAudio::caption() const
{
    return m_caption;
}

int MessageAudio::duration() const
{
    return m_duration;
}

QString MessageAudio::title() const
{
    return m_title;
}

QString MessageAudio::performer() const
{
    return m_performer;
}

QString MessageAudio::fileName() const
{
    return m_fileName;
}

File *MessageAudio::file() const
{
    return m_file.get();
}

const std::shared_ptr<File> &MessageAudio::audioFile() const noexcept
{
    return m_file;
}

void MessageAudio::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

MessageDocument::MessageDocument(td::td_api::object_ptr<td::td_api::messageDocument> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    // Was dereferenced unguarded, which is a crash rather than a missing filename on
    // any message TDLib hands over without a document part.
    if (!content->document_)
        return;

    m_fileName = QString::fromStdString(content->document_->file_name_);

    if (content->document_->document_)
        m_file = std::make_shared<File>(std::move(content->document_->document_));
}

QString MessageDocument::caption() const
{
    return m_caption;
}

QString MessageDocument::fileName() const
{
    return m_fileName;
}

File *MessageDocument::file() const
{
    return m_file.get();
}

const std::shared_ptr<File> &MessageDocument::documentFile() const noexcept
{
    return m_file;
}

void MessageDocument::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

MessagePhoto::MessagePhoto(td::td_api::object_ptr<td::td_api::messagePhoto> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    if (!content->photo_)
        return;

    // Both sizes are identified before anything is moved out: taking a photo_ empties
    // it, and the two picks can land on the same photoSize.
    td::td_api::photoSize *largest = nullptr;
    for (auto &size : content->photo_->sizes_)
    {
        if (size && (!largest || size->width_ > largest->width_))
            largest = size.get();
    }

    auto *display = pickPhotoSize(content->photo_->sizes_);

    // When the two are the same size there is one file, not two objects for one id -
    // which is the bug that used to keep avatars on the placeholder.
    const auto sameFile = display && largest && display == largest;

    if (largest && largest->photo_)
        m_originalFile = std::make_shared<File>(std::move(largest->photo_));

    if (display && (display->photo_ || sameFile))
    {
        m_width = display->width_;
        m_height = display->height_;
        m_file = sameFile ? m_originalFile : std::make_shared<File>(std::move(display->photo_));
    }
}

QString MessagePhoto::caption() const
{
    return m_caption;
}

File *MessagePhoto::file() const
{
    return m_file.get();
}

int MessagePhoto::width() const
{
    return m_width;
}

int MessagePhoto::height() const
{
    return m_height;
}

const std::shared_ptr<File> &MessagePhoto::photoFile() const noexcept
{
    return m_file;
}

void MessagePhoto::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

File *MessagePhoto::originalFile() const
{
    return m_originalFile.get();
}

const std::shared_ptr<File> &MessagePhoto::originalPhotoFile() const noexcept
{
    return m_originalFile;
}

void MessagePhoto::adoptOriginalFile(std::shared_ptr<File> file) noexcept
{
    m_originalFile = std::move(file);
}

MessageSticker::MessageSticker(td::td_api::object_ptr<td::td_api::messageSticker> content, QObject *parent)
    : QObject(parent)
{
    if (!content->sticker_)
        return;

    auto &sticker = content->sticker_;

    m_emoji = QString::fromStdString(sticker->emoji_);
    m_width = sticker->width_;
    m_height = sticker->height_;

    if (sticker->format_)
    {
        switch (sticker->format_->get_id())
        {
            case td::td_api::stickerFormatTgs::ID:
                m_format = QLatin1String("tgs");
                break;
            case td::td_api::stickerFormatWebp::ID:
                m_format = QLatin1String("webp");
                break;
            case td::td_api::stickerFormatWebm::ID:
                m_format = QLatin1String("webm");
                break;
            default:
                break;
        }
    }

    if (sticker->sticker_)
    {
        m_file = std::make_shared<File>(std::move(sticker->sticker_));
    }
}

QString MessageSticker::format() const
{
    return m_format;
}

int MessageSticker::width() const
{
    return m_width;
}

int MessageSticker::height() const
{
    return m_height;
}

File *MessageSticker::file() const
{
    return m_file.get();
}

const std::shared_ptr<File> &MessageSticker::stickerFile() const noexcept
{
    return m_file;
}

void MessageSticker::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

QString MessageSticker::emoji() const
{
    return m_emoji;
}

MessageVideo::MessageVideo(td::td_api::object_ptr<td::td_api::messageVideo> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    // Guarded the same way MessageDocument is: a message TDLib hands over without a
    // video part is a bubble with no still, not a crash.
    if (!content->video_)
        return;

    auto &video = content->video_;

    m_width = video->width_;
    m_height = video->height_;
    m_duration = video->duration_;
    m_fileName = QString::fromStdString(video->file_name_);

    if (video->video_)
        m_file = std::make_shared<File>(std::move(video->video_));

    if (video->thumbnail_ && video->thumbnail_->file_)
    {
        m_thumbnailFormat = decodableThumbnailFormat(*video->thumbnail_);

        if (!m_thumbnailFormat.isEmpty())
            m_thumbnailFile = std::make_shared<File>(std::move(video->thumbnail_->file_));
    }
}

QString MessageVideo::caption() const
{
    return m_caption;
}

File *MessageVideo::file() const
{
    return m_file.get();
}

File *MessageVideo::thumbnailFile() const
{
    return m_thumbnailFile.get();
}

QString MessageVideo::thumbnailFormat() const
{
    return m_thumbnailFormat;
}

int MessageVideo::width() const
{
    return m_width;
}

int MessageVideo::height() const
{
    return m_height;
}

int MessageVideo::duration() const
{
    return m_duration;
}

QString MessageVideo::fileName() const
{
    return m_fileName;
}

const std::shared_ptr<File> &MessageVideo::videoFile() const noexcept
{
    return m_file;
}

void MessageVideo::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

const std::shared_ptr<File> &MessageVideo::videoThumbnailFile() const noexcept
{
    return m_thumbnailFile;
}

void MessageVideo::adoptThumbnailFile(std::shared_ptr<File> file) noexcept
{
    m_thumbnailFile = std::move(file);
}

MessageVideoNote::MessageVideoNote(td::td_api::object_ptr<td::td_api::messageVideoNote> content, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(content)
}

MessageVoiceNote::MessageVoiceNote(td::td_api::object_ptr<td::td_api::messageVoiceNote> content, QObject *parent)
    : QObject(parent)
{
    m_caption = QString::fromStdString(content->caption_->text_);

    // Guarded the same way MessageDocument is: a message TDLib hands over without a voice
    // part is a missing bubble, not a crash.
    if (!content->voice_note_)
        return;

    m_duration = content->voice_note_->duration_;

    if (content->voice_note_->voice_)
        m_file = std::make_shared<File>(std::move(content->voice_note_->voice_));
}

QString MessageVoiceNote::caption() const
{
    return m_caption;
}

File *MessageVoiceNote::file() const
{
    return m_file.get();
}

int MessageVoiceNote::duration() const
{
    return m_duration;
}

const std::shared_ptr<File> &MessageVoiceNote::voiceFile() const noexcept
{
    return m_file;
}

void MessageVoiceNote::adoptFile(std::shared_ptr<File> file) noexcept
{
    m_file = std::move(file);
}

MessageLocation::MessageLocation(td::td_api::object_ptr<td::td_api::messageLocation> content, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(content)
}

MessageVenue::MessageVenue(td::td_api::object_ptr<td::td_api::messageVenue> content, QObject *parent)
    : QObject(parent)
{
    m_venue = QString::fromStdString(content->venue_->title_);
}

QString MessageVenue::venue() const
{
    return m_venue;
}

MessageContact::MessageContact(td::td_api::object_ptr<td::td_api::messageContact> content, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(content)
}

MessageAnimatedEmoji::MessageAnimatedEmoji(td::td_api::object_ptr<td::td_api::messageAnimatedEmoji> content, QObject *parent)
    : QObject(parent)
{
    m_emoji = QString::fromStdString(content->emoji_);
}

QString MessageAnimatedEmoji::text() const
{
    return m_emoji;
}

QString MessageAnimatedEmoji::formattedText() const
{
    // A bare emoji carries no entities, so there is nothing to format.
    return m_emoji;
}

MessagePoll::MessagePoll(td::td_api::object_ptr<td::td_api::messagePoll> content, QObject *parent)
    : QObject(parent)
{
    m_question = QString::fromStdString(content->poll_->question_->text_);
}

QString MessagePoll::question() const
{
    return m_question;
}

MessageInvoice::MessageInvoice(td::td_api::object_ptr<td::td_api::messageInvoice> content, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(content)
}

MessageCall::MessageCall(td::td_api::object_ptr<td::td_api::messageCall> content, QObject *parent)
    : QObject(parent)
{
    m_isVideo = content->is_video_;

    switch (content->discard_reason_->get_id())
    {
        case td::td_api::callDiscardReasonEmpty::ID:
            m_discardReason = DiscardReason::Empty;
            break;
        case td::td_api::callDiscardReasonMissed::ID:
            m_discardReason = DiscardReason::Missed;
            break;
        case td::td_api::callDiscardReasonDeclined::ID:
            m_discardReason = DiscardReason::Declined;
            break;
        case td::td_api::callDiscardReasonDisconnected::ID:
            m_discardReason = DiscardReason::Disconnected;
            break;
        case td::td_api::callDiscardReasonHungUp::ID:
            m_discardReason = DiscardReason::HungUp;
            break;
        default:
            break;
    }

    m_duration = content->duration_;
}

bool MessageCall::isVideo() const
{
    return m_isVideo;
}

MessageCall::DiscardReason MessageCall::discardReason() const
{
    return m_discardReason;
}

int MessageCall::duration() const
{
    return m_duration;
}
