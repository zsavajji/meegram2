#include "MessageModel.hpp"

#include "ChatManager.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "MessageService.hpp"
#include "ScopeTimer.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QDateTime>
#include <QDebug>
#include <QImageReader>
#include <QLocale>

#include <algorithm>
#include <ranges>

namespace {

// One photo, ready to be sent - on its own or as one item of an album.
td::td_api::object_ptr<td::td_api::inputMessagePhoto> makeInputPhoto(const QString &filePath, const QString &caption)
{
    // The file and its dimensions live on a nested inputPhoto; only the caption sits
    // on inputMessagePhoto itself.
    auto photo = td::td_api::make_object<td::td_api::inputPhoto>();

    photo->photo_ = td::td_api::make_object<td::td_api::inputFileLocal>(filePath.toStdString());

    // Read from the header rather than decoded: QImageReader::size() only parses far
    // enough to find the dimensions, which matters for an 8MP shot on this hardware.
    // Zero is acceptable to TDLib; it just means the recipient sees no placeholder
    // geometry until the photo arrives.
    const QImageReader reader(filePath);
    if (const auto size = reader.size(); size.isValid())
    {
        photo->width_ = size.width();
        photo->height_ = size.height();
    }

    auto content = td::td_api::make_object<td::td_api::inputMessagePhoto>();

    content->photo_ = std::move(photo);
    content->caption_ = td::td_api::make_object<td::td_api::formattedText>();
    content->caption_->text_ = caption.toStdString();

    return content;
}

// The server's limit on one album, enforced by TDLib - it rejects the whole request
// past it rather than trimming. The picker stops at the same number.
constexpr int MaxAlbumSize = 10;

}  // namespace

MessageModel::MessageModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_client(storage->client())
    , m_locale(std::move(locale))
    , m_storage(std::move(storage))
    , m_chat(std::move(chat))
{
    connect(m_client.get(), SIGNAL(result(td::td_api::Object *)), SLOT(handleResult(td::td_api::Object *)));

    setRoleNames(roleNames());

    m_historyRetryTimer.setInterval(400);
    m_historyRetryTimer.setSingleShot(true);

    connect(&m_historyRetryTimer, SIGNAL(timeout()), SLOT(reloadHistory()));

    loadMessages();
}

MessageModel::~MessageModel()
{
    // Tell any in-flight getChatHistory callback not to touch this object.
    m_alive->store(false);
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_messages.size();
}

bool MessageModel::canFetchMore(const QModelIndex &parent) const
{
    if (parent.isValid() || m_messages.empty() || m_loading)
        return false;

    // Third site that dereferenced lastMessage() unconditionally; a chat with no last
    // message has nothing newer to fetch.
    const auto *lastMessage = m_chat->lastMessage();
    if (!lastMessage)
        return false;

    return lastMessage->id() != std::ranges::max(m_messages);
}

void MessageModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid() || m_messages.empty() || m_loading)
        return;

    m_loading = true;

    requestHistory(std::ranges::max(m_messages), -MessageSliceLimit, MessageSliceLimit);

    emit loadingChanged();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    // The message list's counterpart to ChatModel::data - the two are what a flick
    // actually costs, so -DMEEGRAM_PROFILE=ON can now see both.
    MEEGRAM_SCOPE("MessageModel::data");

    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_messages.size()))
        return QVariant();

    qlonglong messageId = m_messages[index.row()];
    const auto &message = m_messageMap.at(messageId);

    if (!message)
        return QVariant();

    switch (role)
    {
        case IdRole:
            return message->id();
        case IdStringRole:
            return QString::number(message->id());
        case SenderRole:
            return formattedRow(messageId, message.get()).sender;
        case SenderHtmlRole:
            return formattedRow(messageId, message.get()).senderHtml;
        case ChatIdRole:
            return message->chatId();
        case IsOutgoingRole:
            return message->isOutgoing();
        case DateRole:
            return formattedRow(messageId, message.get()).date;
        case EditDateRole:
            return message->editDate().toString(QObject::tr("formatterDay12H"));
        case ContentRole: {
            if (auto content = message->content())
            {
                switch (message->contentType())
                {
                    case td::td_api::messageText::ID:
                        return QVariant::fromValue(static_cast<MessageText *>(content));
                    case td::td_api::messageAnimation::ID:
                        return QVariant::fromValue(static_cast<MessageAnimation *>(content));
                    case td::td_api::messageAudio::ID:
                        return QVariant::fromValue(static_cast<MessageAudio *>(content));
                    case td::td_api::messageDocument::ID:
                        return QVariant::fromValue(static_cast<MessageDocument *>(content));
                    case td::td_api::messagePhoto::ID:
                        return QVariant::fromValue(static_cast<MessagePhoto *>(content));
                    case td::td_api::messageSticker::ID:
                        return QVariant::fromValue(static_cast<MessageSticker *>(content));
                    case td::td_api::messageVideo::ID:
                        return QVariant::fromValue(static_cast<MessageVideo *>(content));
                    case td::td_api::messageVideoNote::ID:
                        return QVariant::fromValue(static_cast<MessageVideoNote *>(content));
                    case td::td_api::messageVoiceNote::ID:
                        return QVariant::fromValue(static_cast<MessageVoiceNote *>(content));
                    case td::td_api::messageLocation::ID:
                        return QVariant::fromValue(static_cast<MessageLocation *>(content));
                    case td::td_api::messageVenue::ID:
                        return QVariant::fromValue(static_cast<MessageVenue *>(content));
                    case td::td_api::messageContact::ID:
                        return QVariant::fromValue(static_cast<MessageContact *>(content));
                    case td::td_api::messageAnimatedEmoji::ID:
                        return QVariant::fromValue(static_cast<MessageAnimatedEmoji *>(content));
                    case td::td_api::messagePoll::ID:
                        return QVariant::fromValue(static_cast<MessagePoll *>(content));
                    case td::td_api::messageCall::ID:
                        return QVariant::fromValue(static_cast<MessageCall *>(content));
                    default:
                        return QVariant::fromValue(static_cast<MessageService *>(content));
                }
            }
            return QVariant();
        }
        case AlbumRole: {
            QVariantList photos;

            // Only the head of a run of two or more carries the list; the others are drawn
            // by it, and a lone photo stays an ordinary photo bubble.
            if (!sameAlbum(index.row() - 1, index.row()) && sameAlbum(index.row(), index.row() + 1))
            {
                for (int row = index.row(); row < static_cast<int>(m_messages.size()); ++row)
                {
                    photos.append(QVariant::fromValue(static_cast<MessagePhoto *>(m_messageMap.at(m_messages[row])->content())));

                    if (!sameAlbum(row, row + 1))
                        break;
                }
            }

            return photos;
        }
        case AlbumCaptionRole: {
            // The same run AlbumRole walks, and the same head-only condition: only that
            // row draws the batch, so only it has anywhere to put the caption.
            if (sameAlbum(index.row() - 1, index.row()) || !sameAlbum(index.row(), index.row() + 1))
                return QString();

            for (int row = index.row(); row < static_cast<int>(m_messages.size()); ++row)
            {
                // sameAlbum has already established every member of the run is a photo.
                const auto caption = static_cast<const MessagePhoto *>(m_messageMap.at(m_messages[row])->content())->caption();

                if (!caption.isEmpty())
                    return caption;

                if (!sameAlbum(row, row + 1))
                    break;
            }

            return QString();
        }
        case ContentTypeRole: {
            // The delegate picks its component off this role already, so grouping rides on
            // it rather than on two more roles the view would have to read for every row.
            static const QString Album("messageAlbum"), AlbumChild("messageAlbumChild");

            if (sameAlbum(index.row() - 1, index.row()))
                return AlbumChild;

            if (sameAlbum(index.row(), index.row() + 1))
                return Album;

            return message->contentTypeString();
        }
        case IsServiceRole:
            return message->isService();
        case ServiceMessageRole:
            return Utils::getServiceContent(message.get(), m_storage, m_locale, true);
        case SectionRole:
            return formattedRow(messageId, message.get()).section;
        case ReplyToSenderRole:
            return formattedRow(messageId, message.get()).replyToSender;
        case ReplyToTextRole:
            return formattedRow(messageId, message.get()).replyToText;
        case ReplyToMessageIdRole: {
            const auto *reply = message->replyTo();
            // A string, like every other id crossing into QML - see toId() in Common.hpp.
            return reply ? QString::number(reply->messageId) : QString();
        }
        case SendStateRole:
            return sendState(message.get());
    }

    return QVariant();
}

bool MessageModel::sameAlbum(int firstRow, int secondRow) const noexcept
{
    const auto rows = static_cast<int>(m_messages.size());
    if (firstRow < 0 || secondRow < 0 || firstRow >= rows || secondRow >= rows)
        return false;

    const auto &first = m_messageMap.at(m_messages[firstRow]);
    const auto &second = m_messageMap.at(m_messages[secondRow]);

    if (!first || !second || first->mediaAlbumId() == 0 || first->mediaAlbumId() != second->mediaAlbumId())
        return false;

    return first->contentType() == td::td_api::messagePhoto::ID && second->contentType() == td::td_api::messagePhoto::ID;
}

int MessageModel::albumHead(int row) const noexcept
{
    while (sameAlbum(row - 1, row))
        --row;

    return row;
}

void MessageModel::refreshAlbumAt(int row) noexcept
{
    if (!sameAlbum(row - 1, row) && !sameAlbum(row, row + 1))
        return;

    const auto first = albumHead(row);

    auto last = first;
    while (sameAlbum(last, last + 1))
        ++last;

    emit dataChanged(createIndex(first, 0), createIndex(last, 0));
}

QString MessageModel::replyToSender(const Message *message) const noexcept
{
    const auto *reply = message->replyTo();
    if (!reply)
        return {};

    // An explicit origin only appears when the replied-to message came from
    // somewhere else. Prefer it when present.
    if (!reply->hiddenSenderName.isEmpty())
        return reply->hiddenSenderName;

    if (reply->senderUserId != 0)
        return Utils::getUserShortName(m_storage->user(reply->senderUserId));

    if (reply->senderChatId != 0)
        return Utils::getChatTitle(m_storage->chat(reply->senderChatId), m_storage);

    // Ordinary same-chat reply: no origin, so resolve the sender from the
    // replied-to message if it happens to be one we have already loaded.
    if (const auto it = m_messageMap.find(reply->messageId); it != m_messageMap.end() && it->second)
        return Utils::getSenderName(it->second.get(), m_storage);

    return {};
}

QString MessageModel::replyToText(const Message *message) const noexcept
{
    const auto *reply = message->replyTo();
    if (!reply)
        return {};

    // A manually selected quote is what the sender chose to point at, so it beats
    // the generated preview.
    if (!reply->quote.isEmpty())
        return reply->quote;

    if (reply->content)
    {
        // isOutgoing is only used by call-content wording, which never appears in a
        // reply preview; false is the neutral choice.
        return Utils::getContent(reply->content.get(), reply->contentType, false, m_locale);
    }

    // messageReplyToMessage only carries content when the replied-to message came from
    // another chat - for a reply within this chat it is null, which is why the quote
    // block showed a sender and no text. Resolve it from the loaded messages, the same
    // fallback replyToSender already makes.
    //
    // ponytail: only messages currently held by the model. Replying to something far
    // enough back that it has not been loaded still shows nothing; getMessage plus a
    // dataChanged when it lands is the upgrade.
    if (const auto it = m_messageMap.find(reply->messageId); it != m_messageMap.end() && it->second)
        return Utils::getContent(it->second.get(), m_storage, m_locale);

    return {};
}

QString MessageModel::sendState(const Message *message) const noexcept
{
    // Nothing to say about a message somebody else sent, and a service message is not
    // sent by anyone. Both hide the indicator by giving the delegate an empty string.
    if (!message->isOutgoing() || message->isService())
        return {};

    // Shared instances rather than four literals: this role is read on every rebind while
    // the list flicks, and the other roles hand back strings the row cache already holds.
    static const QString Failed("failed"), Sending("sending"), Read("read"), Sent("sent");

    if (message->isFailed())
        return Failed;

    if (message->isPending())
        return Sending;

    // The read pointer is a single "everything up to here" id, the outgoing counterpart
    // of the one viewMessagesUpTo moves on the other side.
    return message->id() <= m_chat->lastReadOutboxMessageId() ? Read : Sent;
}

const MessageModel::FormattedRow &MessageModel::formattedRow(qlonglong messageId, const Message *message) const noexcept
{
    auto &entry = m_formatted[messageId];

    if (!entry.valid)
    {
        // calls = messages formatted from scratch, against MessageModel::data's calls
        // for the hit rate. Included in data's total, so never add the two rows together.
        MEEGRAM_SCOPE("MessageModel::formattedRow.miss");

        entry.sender = Utils::getSenderName(message, m_storage);

        // Same reason as ChatModel's titleHtml: this ran from a QML binding in the
        // bubble, so it re-ran on every rebind regardless of this cache
        // (docs/profiling.md).
        entry.senderHtml = Utils::replaceEmoji(entry.sender);

        entry.date = message->date().toString(QObject::tr("formatterDay12H"));
        entry.section = sectionFor(message);
        entry.replyToSender = replyToSender(message);
        entry.replyToText = replyToText(message);
        entry.valid = true;
    }

    return entry;
}

QString MessageModel::sectionFor(const Message *message) const noexcept
{
    // ponytail: sampled once per process, so a session left open across midnight keeps
    // calling yesterday "Today". Pre-dates the cache; a date-change timer that clears
    // m_formatted is the upgrade.
    static const auto currentDateTime = QDateTime::currentDateTime();
    const auto days = message->date().daysTo(currentDateTime);

    if (days == 0)
        return QObject::tr("MessageScheduleToday");

    if (days == 1)
        return QObject::tr("Yesterday");

    return message->date().toString(QObject::tr("chatFullDate"));
}

QHash<int, QByteArray> MessageModel::roleNames() const noexcept
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[IdStringRole] = "idString";
    roles[SenderRole] = "sender";
    roles[SenderHtmlRole] = "senderHtml";
    roles[ChatIdRole] = "chatId";
    roles[IsOutgoingRole] = "isOutgoing";
    roles[DateRole] = "date";
    roles[EditDateRole] = "editDate";
    roles[ContentRole] = "content";
    roles[AlbumRole] = "album";
    roles[AlbumCaptionRole] = "albumCaption";
    // Custom
    roles[ContentTypeRole] = "contentType";
    roles[IsServiceRole] = "isService";
    roles[ServiceMessageRole] = "serviceMessage";
    roles[SectionRole] = "section";
    roles[ReplyToSenderRole] = "replyToSender";
    roles[ReplyToTextRole] = "replyToText";
    roles[ReplyToMessageIdRole] = "replyToMessageId";
    roles[SendStateRole] = "sendState";
    return roles;
}

int MessageModel::count() const noexcept
{
    return m_messages.size();
}

bool MessageModel::loading() const noexcept
{
    // Both directions, not just m_loading. Paging up runs fetchMoreBack/m_backFetching,
    // so a property that only reported m_loading left the header spinner dark for the
    // one wait the user actually sits through - scrolling back into history.
    return m_loading || m_backFetching;
}

void MessageModel::getChatHistory(const QString &fromMessageId, int offset, int limit, bool fetchPrevious) noexcept
{
    requestHistory(toId(fromMessageId), offset, limit, fetchPrevious);
}

void MessageModel::requestHistory(qlonglong fromMessageId, int offset, int limit, bool fetchPrevious) noexcept
{
    auto request = td::td_api::make_object<td::td_api::getChatHistory>();
    request->chat_id_ = m_chat->id();
    request->from_message_id_ = fromMessageId;
    request->offset_ = offset;
    request->limit_ = limit;
    request->only_local_ = false;

    m_client->send(std::move(request), [this, fetchPrevious, alive = m_alive](auto &&response) {
        // Runs on the TDLib worker thread, and the model may already be gone: leaving a
        // chat destroys it, and a history request is usually still outstanding when you
        // do. This is the one member access left here, and it guards the `this` below.
        if (!alive->load())
            return;

        // Nothing else is touched on this thread. The body used to run inline here -
        // filling m_messageMap, inserting into m_messages, driving begin/endInsertRows -
        // while the GUI thread sat in data() reading all three. A vector reallocating or
        // a map rehashing under a concurrent reader is a use-after-free, and scrolling is
        // exactly what triggers the fetch, so it landed while delegates were being built.
        // handleHistoryResponse already named that hazard for StorageManager's file map and
        // hopped only that one call, while the lines right above it did the same thing to
        // this model's own containers.
        //
        // Same hop ChatModel::handleChatsLoaded makes, and the same void* handover
        // Client::disposeObject uses: a queued Q_ARG needs a registered metatype, and
        // td_api::object_ptr is move-only.
        //
        // ponytail: if the model dies between the check above and the queued call landing,
        // this response leaks - one object, bounded by chats opened. Hand over a shared_ptr
        // if that ever shows up in a measurement.
        QMetaObject::invokeMethod(this, "handleHistoryResponse", Qt::QueuedConnection, Q_ARG(void *, response.release()),
                                  Q_ARG(bool, fetchPrevious));
    });
}

void MessageModel::handleHistoryResponse(void *responseObject, bool fetchPrevious) noexcept
{
    // Ownership arrives here. Everything below runs on the GUI thread, so it may touch the
    // model freely - which is the whole point of the hop.
    td::td_api::object_ptr<td::td_api::Object> response(static_cast<td::td_api::Object *>(responseObject));

    if (!response)
        return;

    auto cleanupFlags = [this]() {
        if (m_loading || m_backFetching)
        {
            m_loading = false;
            m_backFetching = false;
            emit loadingChanged();
        }

        emit countChanged();
    };

    if (response->get_id() != td::td_api::messages::ID)
    {
        // qWarning, not qDebug: release builds define QT_NO_DEBUG_OUTPUT, so a rejected
        // request left no trace at all and the chat just span forever.
        if (response->get_id() == td::td_api::error::ID)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());
            qWarning() << "getChatHistory failed:" << error->code_ << QString::fromStdString(error->message_);
        }

        cleanupFlags();
        return;
    }

    auto messagesResponse = td::td_api::move_object_as<td::td_api::messages>(response);
    if (!messagesResponse || messagesResponse->messages_.empty())
    {
        // An empty answer does not mean the chat is empty. TDLib may return fewer messages
        // than asked for - or none - while its own fetch is still in flight, and expects
        // the request to be repeated; for a supergroup there is often nothing cached
        // locally to answer from on the first call. Treating empty as final is what leaves
        // a group on a spinner forever.
        //
        // m_loading stays set, so the view keeps showing it is still working.
        m_historyRetryTimer.start();
        return;
    }

    m_historyRetries = 0;

    std::vector<qlonglong> newMessageIds;
    for (auto &&message : messagesResponse->messages_)
    {
        const auto messageId = message->id_;
        if (!m_messageMap.contains(messageId))
        {
            newMessageIds.emplace_back(messageId);
            m_messageMap[messageId] = std::make_unique<Message>(std::move(message));
        }
    }

    if (!newMessageIds.empty())
    {
        insertMessages(std::move(newMessageIds), fetchPrevious);
    }

    // linkContentFile reaches into StorageManager's file map, which the main thread mutates
    // on every updateFile - two threads doing try_emplace on one unordered_map can leave a
    // reader walking a broken bucket chain forever. That is no longer a risk now this runs
    // on the GUI thread, so the hop it used to need is gone.
    linkLoadedContentFiles();

    cleanupFlags();
}

void MessageModel::send(td::td_api::object_ptr<td::td_api::InputMessageContent> content, qlonglong replyToMessageId) noexcept
{
    auto request = td::td_api::make_object<td::td_api::sendMessage>();

    request->chat_id_ = m_chat->id();

    if (replyToMessageId != 0)
    {
        // Field order is message_id, quote, checklist_task_id, poll_option_id.
        // The chat_id parameter this used to take was removed from td_api - the
        // reply target is always in the chat the message is sent to.
        request->reply_to_ = td::td_api::make_object<td::td_api::inputMessageReplyToMessage>(replyToMessageId, nullptr, 0, "");
    }

    request->input_message_content_ = std::move(content);

    m_client->send(std::move(request));
}

void MessageModel::sendMessage(const QString &message, const QString &replyToMessageId, const QString &mentionUserIds,
                               const QString &mentionNames) noexcept
{
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();

    content->text_ = td::td_api::make_object<td::td_api::formattedText>();
    content->text_->text_ = message.toStdString();

    const auto ids = mentionUserIds.split(QLatin1Char('\n'), QString::SkipEmptyParts);
    const auto names = mentionNames.split(QLatin1Char('\n'), QString::SkipEmptyParts);

    // Where each picked name ended up, found now rather than tracked while the message
    // was being typed. A name the user has since edited finds no match and goes out as
    // plain text, which is the right answer for words that are no longer that person's.
    QList<int> taken;

    for (int i = 0; i < ids.size() && i < names.size(); ++i)
    {
        int offset = 0;

        // Two people with the same name, or one mentioned twice: each entity takes the
        // next occurrence rather than all of them landing on the first.
        for (int from = 0; (offset = message.indexOf(names.at(i), from)) >= 0 && taken.contains(offset); from = offset + 1)
        {
        }

        if (offset < 0)
            continue;

        taken.append(offset);

        auto entity = td::td_api::make_object<td::td_api::textEntity>();

        // TDLib counts in UTF-16 code units, which is exactly what QString indexes in.
        entity->offset_ = offset;
        entity->length_ = names.at(i).length();
        entity->type_ = td::td_api::make_object<td::td_api::textEntityTypeMentionName>(ids.at(i).toLongLong());

        content->text_->entities_.push_back(std::move(entity));
    }

    // TDLib rejects a formattedText whose entities are out of order, and the picked
    // names are in the order they were chosen rather than where they sit.
    std::ranges::sort(content->text_->entities_, {}, [](const auto &entity) { return entity->offset_; });

    send(std::move(content), toId(replyToMessageId));
}

void MessageModel::sendPhoto(const QString &filePath, const QString &caption, const QString &replyToMessageId) noexcept
{
    send(makeInputPhoto(filePath, caption), toId(replyToMessageId));
}

void MessageModel::sendPhotos(const QString &filePaths, const QString &caption, const QString &replyToMessageId) noexcept
{
    const auto paths = filePaths.split(QLatin1Char('\n'), QString::SkipEmptyParts);

    if (paths.isEmpty())
        return;

    // One photo is an ordinary message rather than an album of one - and it is what the
    // picker sends when only one thumbnail was ticked.
    if (paths.size() == 1)
    {
        sendPhoto(paths.first(), caption, replyToMessageId);
        return;
    }

    auto request = td::td_api::make_object<td::td_api::sendMessageAlbum>();

    request->chat_id_ = m_chat->id();

    if (const auto replyTo = toId(replyToMessageId); replyTo != 0)
    {
        // Same shape as send() above: the reply target is always in the chat being sent to.
        request->reply_to_ = td::td_api::make_object<td::td_api::inputMessageReplyToMessage>(replyTo, nullptr, 0, "");
    }

    for (int i = 0; i < paths.size() && i < MaxAlbumSize; ++i)
    {
        // The caption goes on the first photo alone. That is where Telegram puts an
        // album's text, and where the bubble looks for it - putting it on every one
        // would repeat it under the mosaic.
        request->input_message_contents_.push_back(makeInputPhoto(paths.at(i), i == 0 ? caption : QString()));
    }

    m_client->send(std::move(request));
}

void MessageModel::sendDocument(const QString &filePath, const QString &caption, const QString &replyToMessageId) noexcept
{
    // No thumbnail and content-type detection left on: TDLib sniffs the mime type from
    // the file itself, which is the whole point of picking an arbitrary file.
    auto document = td::td_api::make_object<td::td_api::inputDocument>();

    document->document_ = td::td_api::make_object<td::td_api::inputFileLocal>(filePath.toStdString());

    auto content = td::td_api::make_object<td::td_api::inputMessageDocument>();

    content->document_ = std::move(document);
    content->caption_ = td::td_api::make_object<td::td_api::formattedText>();
    content->caption_->text_ = caption.toStdString();

    send(std::move(content), toId(replyToMessageId));
}

void MessageModel::sendVoiceNote(const QString &filePath, int duration, const QString &caption, const QString &replyToMessageId) noexcept
{
    auto voiceNote = td::td_api::make_object<td::td_api::inputVoiceNote>();

    voiceNote->voice_note_ = td::td_api::make_object<td::td_api::inputFileLocal>(filePath.toStdString());
    voiceNote->duration_ = duration;

    // waveform_ left empty, which TDLib accepts. It is the little bar chart other clients
    // draw behind the play button, and this one draws no waveform to fill in.

    auto content = td::td_api::make_object<td::td_api::inputMessageVoiceNote>();

    content->voice_note_ = std::move(voiceNote);
    content->caption_ = td::td_api::make_object<td::td_api::formattedText>();
    content->caption_->text_ = caption.toStdString();

    send(std::move(content), toId(replyToMessageId));
}

void MessageModel::fetchMoreBack() noexcept
{
    if (m_backFetching || m_messages.empty())
        return;

    m_backFetching = true;

    requestHistory(std::ranges::min(m_messages), 0, MessageSliceLimit, true);

    emit loadingChanged();
}

void MessageModel::viewMessagesUpTo(int index) noexcept
{
    if (index < 0 || index >= static_cast<int>(m_messages.size()))
        return;

    const auto messageId = m_messages.at(index);

    // Inbox read state is a single "last read" pointer, so reporting the newest
    // message on screen marks everything before it read as well. The comparison also
    // keeps this from re-sending on every scroll once the chat is fully read.
    if (messageId <= m_chat->lastReadInboxMessageId())
        return;

    auto request = td::td_api::make_object<td::td_api::viewMessages>();

    request->chat_id_ = m_chat->id();
    request->message_ids_ = {messageId};
    // Left null this meant "guess from the chat's open state", and the guess depends on
    // openChat having been processed before the view is reported. Saying it outright is
    // what makes the read reach other clients.
    request->source_ = td::td_api::make_object<td::td_api::messageSourceChatHistory>();
    request->force_read_ = true;

    m_client->send(std::move(request));
}

void MessageModel::openMessageContent(const QString &rawMessageId) noexcept
{
    const auto messageId = toId(rawMessageId);

    const auto it = m_messageMap.find(messageId);
    // Own messages are opened by definition, and TDLib errors on them.
    if (it == m_messageMap.end() || it->second->isOutgoing())
        return;

    auto request = td::td_api::make_object<td::td_api::openMessageContent>();

    request->chat_id_ = m_chat->id();
    request->message_id_ = messageId;

    m_client->send(std::move(request));
}

void MessageModel::deleteMessage(const QString &rawMessageId, bool revoke) noexcept
{
    const auto messageId = toId(rawMessageId);

    auto request = td::td_api::make_object<td::td_api::deleteMessages>();

    request->chat_id_ = m_chat->id();
    // Was vector<int64_t>(messageId) - the count constructor, which asked for a
    // vector of messageId zeroed elements. Message ids run into the billions.
    request->message_ids_ = {messageId};
    request->revoke_ = revoke;

    m_client->send(std::move(request));
}

void MessageModel::linkLoadedContentFiles() noexcept
{
    for (const auto &[messageId, message] : m_messageMap)
    {
        linkContentFile(message.get());
    }
}

void MessageModel::linkContentFile(Message *message) noexcept
{
    if (!message || !message->content())
        return;

    switch (message->contentType())
    {
        case td::td_api::messagePhoto::ID: {
            auto *photo = static_cast<MessagePhoto *>(message->content());
            photo->adoptFile(m_storage->registerFile(photo->photoFile()));
            // The original too, or its download would never reach the object Save is
            // bound to. registerFile hands back whichever instance is canonical, so
            // when both sizes are the same file this simply re-adopts the same one.
            photo->adoptOriginalFile(m_storage->registerFile(photo->originalPhotoFile()));
            break;
        }
        case td::td_api::messageSticker::ID: {
            auto *sticker = static_cast<MessageSticker *>(message->content());
            sticker->adoptFile(m_storage->registerFile(sticker->stickerFile()));
            break;
        }
        case td::td_api::messageDocument::ID: {
            auto *document = static_cast<MessageDocument *>(message->content());
            document->adoptFile(m_storage->registerFile(document->documentFile()));
            break;
        }
        case td::td_api::messageAudio::ID: {
            auto *audio = static_cast<MessageAudio *>(message->content());
            audio->adoptFile(m_storage->registerFile(audio->audioFile()));
            break;
        }
        case td::td_api::messageVoiceNote::ID: {
            auto *voiceNote = static_cast<MessageVoiceNote *>(message->content());
            voiceNote->adoptFile(m_storage->registerFile(voiceNote->voiceFile()));
            break;
        }
        case td::td_api::messageVideo::ID: {
            auto *video = static_cast<MessageVideo *>(message->content());
            video->adoptFile(m_storage->registerFile(video->videoFile()));
            // The still too, or its download would never reach the object the
            // placeholder is bound to and the bubble would sit on the scrim forever.
            video->adoptThumbnailFile(m_storage->registerFile(video->videoThumbnailFile()));
            break;
        }
        case td::td_api::messageAnimation::ID: {
            auto *animation = static_cast<MessageAnimation *>(message->content());
            animation->adoptFile(m_storage->registerFile(animation->animationFile()));
            animation->adoptThumbnailFile(m_storage->registerFile(animation->animationThumbnailFile()));
            break;
        }
        default:
            break;
    }
}

void MessageModel::editMessage(const QString &rawMessageId, const QString &text) noexcept
{
    const auto messageId = toId(rawMessageId);

    auto formatted = td::td_api::make_object<td::td_api::formattedText>();
    formatted->text_ = text.toStdString();

    const auto it = m_messageMap.find(messageId);

    // A photo's text is its caption, and TDLib rejects editMessageText for anything
    // that is not a text message - so the menu's Edit entry would have looked like it
    // worked and done nothing. messageAnimatedEmoji counts as text: it is a one-emoji
    // text message that TDLib reports under another content type, and it has no caption
    // for editMessageCaption to edit.
    if (it != m_messageMap.end() && it->second->contentType() != td::td_api::messageText::ID &&
        it->second->contentType() != td::td_api::messageAnimatedEmoji::ID)
    {
        auto request = td::td_api::make_object<td::td_api::editMessageCaption>();

        request->chat_id_ = m_chat->id();
        request->message_id_ = messageId;
        request->caption_ = std::move(formatted);

        m_client->send(std::move(request));
        return;
    }

    auto request = td::td_api::make_object<td::td_api::editMessageText>();

    auto inputMessageContent = td::td_api::make_object<td::td_api::inputMessageText>();
    inputMessageContent->text_ = std::move(formatted);

    request->chat_id_ = m_chat->id();
    request->message_id_ = messageId;
    request->input_message_content_ = std::move(inputMessageContent);

    m_client->send(std::move(request));
}

void MessageModel::refresh() noexcept
{
    if (m_messages.empty())
        return;

    m_loading = true;
    m_backFetching = true;

    beginResetModel();
    m_messages.clear();
    m_messageMap.clear();
    m_formatted.clear();
    endResetModel();

    emit countChanged();
}

void MessageModel::handleResult(td::td_api::Object *object) noexcept
{
    switch (object->get_id())
    {
        case td::td_api::updateNewMessage::ID: {
            auto update = static_cast<td::td_api::updateNewMessage *>(object);
            handleNewMessage(std::move(update->message_));
            break;
        }
        case td::td_api::updateMessageSendSucceeded::ID: {
            auto update = static_cast<td::td_api::updateMessageSendSucceeded *>(object);
            handleMessageSendCompleted(std::move(update->message_), update->old_message_id_);
            break;
        }
        case td::td_api::updateMessageSendFailed::ID: {
            auto update = static_cast<td::td_api::updateMessageSendFailed *>(object);
            handleMessageSendCompleted(std::move(update->message_), update->old_message_id_);
            break;
        }
        case td::td_api::updateMessageContent::ID: {
            auto update = static_cast<td::td_api::updateMessageContent *>(object);
            handleMessageContent(update->chat_id_, update->message_id_, std::move(update->new_content_));
            break;
        }
        case td::td_api::updateMessageEdited::ID: {
            auto update = static_cast<td::td_api::updateMessageEdited *>(object);
            handleMessageEdited(update->chat_id_, update->message_id_, update->edit_date_, std::move(update->reply_markup_));
            break;
        }
        case td::td_api::updateChatReadOutbox::ID: {
            auto update = static_cast<td::td_api::updateChatReadOutbox *>(object);
            handleChatReadOutbox(update->chat_id_, update->last_read_outbox_message_id_);
            break;
        }
        case td::td_api::updateDeleteMessages::ID: {
            auto update = static_cast<td::td_api::updateDeleteMessages *>(object);
            handleDeleteMessages(update->chat_id_, std::move(update->message_ids_), update->is_permanent_, update->from_cache_);
            break;
        }
        default:
            break;
    }
}

void MessageModel::handleNewMessage(td::td_api::object_ptr<td::td_api::message> &&message) noexcept
{
    if (m_chat->id() != message->chat_id_)
        return;

    auto messageId = message->id_;

    if (m_messageMap.contains(messageId))
        return;

    auto it = std::ranges::lower_bound(m_messages, messageId);
    auto pos = static_cast<int>(std::distance(m_messages.begin(), it));

    beginInsertRows(QModelIndex(), pos, pos);

    m_messages.insert(it, messageId);
    m_messageMap[messageId] = std::make_unique<Message>(std::move(message));
    linkContentFile(m_messageMap[messageId].get());

    endInsertRows();

    // An album arrives as one updateNewMessage per photo, so the row above may have just
    // become an album, or grown by one.
    refreshAlbumAt(pos);

    emit countChanged();

    // Messages sort by id and a new one always has the highest, so this is the last
    // row - but say so explicitly rather than leave the view to assume it.
    if (pos == static_cast<int>(m_messages.size()) - 1)
    {
        emit messageAppended();
    }
}

void MessageModel::handleMessageSendCompleted(td::td_api::object_ptr<td::td_api::message> &&message, qlonglong oldMessageId) noexcept
{
    if (!message || m_chat->id() != message->chat_id_)
        return;

    const auto newMessageId = message->id_;

    // Sending a message puts it in the model twice. updateNewMessage delivers it straight
    // away with a temporary id, and when the server acks it TDLib re-issues the same
    // message under its real id here - the temporary one is never deleted, it is retired
    // by this update and nothing else. Ignoring it left the pending copy in m_messages
    // under an id no later fetch can match, so the next getChatHistory - the retry after
    // an empty reply, on this very chat - saw the real message as new and appended it
    // beside the copy already on screen. Reopening the chat rebuilt the model from the
    // server, which is why that cleared it.
    const auto it = std::ranges::find(m_messages, oldMessageId);
    if (it == m_messages.end())
    {
        handleNewMessage(std::move(message));
        return;
    }

    const auto pos = std::distance(m_messages.begin(), it);

    // The pending Message owns the content object the delegate is bound to, so it has to
    // outlive the row's update: itemChanged below is what makes QML re-read the row and
    // let go of that pointer. Destroyed on the way out of this function instead.
    auto pending = std::move(m_messageMap[oldMessageId]);

    m_messageMap.erase(oldMessageId);
    m_formatted.erase(oldMessageId);

    m_messageMap[newMessageId] = std::make_unique<Message>(std::move(message));
    linkContentFile(m_messageMap[newMessageId].get());

    // The real id is above the temporary one, so the row keeps its place - unless a
    // second message is still pending behind it, which happens when two are sent in
    // quick succession: the first gets a server id larger than the temporary id the
    // second still holds. m_messages has to stay sorted, so that case re-sorts.
    if (it + 1 == m_messages.end() || newMessageId < *(it + 1))
    {
        *it = newMessageId;
        itemChanged(pos);
        return;
    }

    beginResetModel();
    *it = newMessageId;
    std::ranges::sort(m_messages);
    endResetModel();
}

void MessageModel::handleMessageContent(qlonglong chatId, qlonglong messageId, td::td_api::object_ptr<td::td_api::MessageContent> &&newContent) noexcept
{
    if (chatId != m_chat->id())
        return;

    if (auto it = m_messageMap.find(messageId); it != m_messageMap.end())
    {
        it->second->setContent(std::move(newContent));
        // setContent builds a fresh content object, so its File needs linking again.
        linkContentFile(it->second.get());

        const auto row = static_cast<int>(std::distance(m_messages.begin(), std::ranges::find(m_messages, messageId)));

        itemChanged(row);
        // An album member is drawn by the head row, not by its own.
        refreshAlbumAt(row);
    }
}

void MessageModel::handleMessageEdited(qlonglong chatId, qlonglong messageId, int editDate,
                                       td::td_api::object_ptr<td::td_api::ReplyMarkup> &&replyMarkup) noexcept
{
    Q_UNUSED(replyMarkup)

    if (chatId != m_chat->id())
        return;

    if (auto it = m_messageMap.find(messageId); it != m_messageMap.end())
    {
        it->second->setEditDate(editDate);

        itemChanged(std::distance(m_messages.begin(), std::ranges::find(m_messages, messageId)));
    }
}

void MessageModel::handleChatReadOutbox(qlonglong chatId, qlonglong lastReadOutboxMessageId) noexcept
{
    if (chatId != m_chat->id() || m_messages.empty())
        return;

    // sendState reads the pointer off the Chat, which StorageManager owns and updates from
    // this same update - and it is connected to the client first, at startup, so it has
    // already run by the time this does. Nothing to store here, only rows to repaint.
    const auto last = std::ranges::upper_bound(m_messages, lastReadOutboxMessageId);
    if (last == m_messages.begin())
        return;

    // Everything up to the pointer, rather than only what it just passed: the previous
    // value is not kept, and rows already green re-read to the same string. Only the rows
    // on screen actually re-read at all.
    const auto lastRow = static_cast<int>(std::distance(m_messages.begin(), last)) - 1;

    emit dataChanged(createIndex(0, 0), createIndex(lastRow, 0));
}

void MessageModel::handleDeleteMessages(qlonglong chatId, std::vector<int64_t> &&messageIds, bool isPermanent, bool fromCache) noexcept
{
    Q_UNUSED(isPermanent)

    if (chatId != m_chat->id())
        return;

    // TDLib dropped these from its own memory. They are still in the chat, and it says so:
    // from_cache means "no longer loaded", not "deleted". The two arrive as the same update
    // and this treated them alike, so rows vanished from under the user.
    //
    // What arms it is closeChat - MessagesManager::close_dialog sets the unload timeout and
    // unload_dialog then sends updateDeleteMessages with is_permanent false and from_cache
    // true. ChatManager::eventFilter sends closeChat when the app is *minimised*, on
    // purpose, so this fires without leaving the chat page at all: minimise, come back,
    // messages gone. Reopening the chat rebuilt the model, which is why that looked like a
    // fix.
    //
    // Nothing to do here. The model owns its own Message objects, so TDLib forgetting them
    // costs this side nothing, and anything scrolled to afterwards is fetched again.
    if (fromCache)
        return;

    std::unordered_set idsToDelete(messageIds.begin(), messageIds.end());

    std::vector<int> indicesToRemove;
    for (int i = 0; i < static_cast<int>(m_messages.size()); ++i)
    {
        if (idsToDelete.contains(m_messages[i]))
        {
            indicesToRemove.emplace_back(i);
        }
    }

    if (indicesToRemove.empty())
        return;

    std::ranges::sort(indicesToRemove, std::less<>());

    beginRemoveRows(QModelIndex(), indicesToRemove.front(), indicesToRemove.back());

    std::erase_if(m_messages, [&idsToDelete](const auto &id) { return idsToDelete.contains(id); });
    std::erase_if(m_messageMap, [&idsToDelete](const auto &pair) { return idsToDelete.contains(pair.first); });
    std::erase_if(m_formatted, [&idsToDelete](const auto &pair) { return idsToDelete.contains(pair.first); });

    endRemoveRows();

    // Deleting one photo of an album leaves the rest of the run to redraw - and if the
    // head went, the row that took its place is the new head.
    refreshAlbumAt(indicesToRemove.front());
}

void MessageModel::reloadHistory() noexcept
{
    if (m_historyRetries >= MaxHistoryRetries)
    {
        qWarning() << "getChatHistory: gave up after" << m_historyRetries << "empty replies for chat" << m_chat->id();

        if (m_loading || m_backFetching)
        {
            m_loading = false;
            m_backFetching = false;
            emit loadingChanged();
        }

        emit countChanged();
        return;
    }

    ++m_historyRetries;

    // The retry has to ask the same end of the chat the empty reply came from.
    // loadMessages() unconditionally requests the newest slice, so an empty answer to a
    // page-up was retried by re-fetching messages already loaded: no new ids, so
    // insertMessages returned early and never emitted fetchedPosition, and cleanupFlags
    // cleared m_backFetching anyway. The page the user scrolled for was dropped and the
    // list just stopped, until they flicked again. Both flags set is the opening load,
    // which is the newest slice by definition.
    if (m_backFetching && !m_loading && !m_messages.empty())
        requestHistory(std::ranges::min(m_messages), 0, MessageSliceLimit, true);
    else
        loadMessages();
}

void MessageModel::loadMessages() noexcept
{
    // Always the newest slice. This used to anchor on lastReadInboxMessageId with a
    // negative offset when the chat had unread messages, so opening a chat with more
    // unread than one slice loaded a window that did not contain the newest message at
    // all - the bottom of the list was not the last message received.
    //
    // 0, not the chat's last message id. With offset 0 getChatHistory returns messages
    // strictly *older* than from_message_id: OrderedMessages::get_history steps the
    // iterator back one as soon as it lands on that id (td/telegram/OrderedMessage.cpp,
    // the `--it` under `(*it)->message_id_ == from_message_id`), which is what makes
    // fetchMoreBack's "from the oldest loaded" paging work without duplicating a row.
    // Anchoring the opening request on the last message therefore asked for everything
    // except the message the user opened the chat to read. It only appeared later - when
    // a live updateNewMessage appended it, or when something newer had arrived by the
    // time the chat was reopened.
    //
    // 0 means "from the newest" and includes it. It also covers the chat that has no last
    // message at all - freshly created, or its history cleared - which is why this
    // stopped dereferencing lastMessage().
    //
    // Not the smaller-looking fix, which is to keep the anchor and pass offset -1: that is
    // inclusive too, and it keeps the 50-message server request the old call made. It also
    // reaches CHECK(!have_a_gap) in OrderedMessages::get_history, which aborts the process
    // when the anchor is the dialog's last message and that message is not in TDLib's
    // loaded set. ChatManager sends closeChat on minimise, which is what arms the dialog
    // unload that produces exactly that state - see handleDeleteMessages on from_cache. 0
    // takes the from-the-end path and never runs that code.
    requestHistory(0, 0, MessageSliceLimit);
}

int MessageModel::lastMessageIndex() const noexcept
{
    // find() returns end() - i.e. count() - when the last read message is not in the
    // loaded slice, which is exactly when the view must fall back to the newest
    // message rather than anchor on whatever happens to be loaded.
    return std::distance(m_messages.begin(), std::ranges::find(m_messages, m_chat->lastReadInboxMessageId()));
}

int MessageModel::indexOf(const QString &messageId) const noexcept
{
    // -1 rather than count(): the caller jumps to the row, and "not loaded" has to be
    // distinguishable from a real one. Ids are sorted ascending and only ever grow, but
    // a linear scan over one loaded slice is not worth a binary search.
    const auto it = std::ranges::find(m_messages, toId(messageId));

    return it == m_messages.end() ? -1 : static_cast<int>(std::distance(m_messages.begin(), it));
}

void MessageModel::itemChanged(size_t index) noexcept
{
    // Every caller reaches here after mutating the message, so this is the one place
    // the row's formatted values have to be dropped. Doing it in the handlers instead
    // would mean a new handler could forget to.
    if (index < m_messages.size())
        m_formatted.erase(m_messages[index]);

    QModelIndex modelIndex = createIndex(static_cast<int>(index), 0);

    emit dataChanged(modelIndex, modelIndex);
}

void MessageModel::insertMessages(std::vector<qlonglong> &&newIds, bool prepend)
{
    if (newIds.empty())
        return;

    std::ranges::sort(newIds);

    // A reply preview resolves against the messages currently loaded, so a page of
    // older history can answer a quote that came back empty before it arrived.
    // Cheaper to drop the lot on a page fetch than to work out which rows care.
    m_formatted.clear();

    // A row insertion can only describe a page that lands wholly at one end: rowCount grows
    // there and every row already in the model keeps its index. That is the usual shape -
    // fetchMoreBack asks for ids below the oldest loaded, fetchMore for ids above the
    // newest - but it was assumed rather than checked. The old code inserted at one end,
    // signalled, and only *then* ran inplace_merge across the whole vector, so the view was
    // told rows had appeared in one place while the data quietly moved them somewhere else.
    // A QML1 ListView keeps the delegates it has already built for those indices, which is
    // how one message ended up on screen twice.
    //
    // Two ways in. A message arriving while the first history request is in flight puts a
    // newer id in the model before a page of older ones is appended "at the end". And until
    // handleDeleteMessages learned to ignore from_cache deletions, every minimise punched
    // holes through the middle of the loaded range for the next page to land in.
    const bool atOneEnd = m_messages.empty() || (prepend ? newIds.back() < m_messages.front() : newIds.front() > m_messages.back());

    if (!atOneEnd)
    {
        // Rows land in the middle, so every index after each one shifts and a reset is the
        // only honest signal. Rare by construction, and the alternative - one insertion per
        // contiguous run - is far easier to get wrong than this is to pay for.
        beginResetModel();

        m_messages.insert(m_messages.end(), newIds.begin(), newIds.end());
        std::ranges::sort(m_messages);

        endResetModel();
    }
    else if (prepend)
    {
        beginInsertRows(QModelIndex(), 0, static_cast<int>(newIds.size()) - 1);
        m_messages.insert(m_messages.begin(), newIds.begin(), newIds.end());
        endInsertRows();
    }
    else
    {
        const auto pos = static_cast<int>(m_messages.size());
        beginInsertRows(QModelIndex(), pos, pos + static_cast<int>(newIds.size()) - 1);
        m_messages.insert(m_messages.end(), newIds.begin(), newIds.end());
        endInsertRows();
    }

    // A page can land with an album split across its edge: older history bringing the first
    // photos of a run whose tail was already loaded, or the reverse. The row where the block
    // meets what was already there is the one whose album changed shape. (The reset branch
    // above needs nothing - the view re-reads every row anyway.)
    refreshAlbumAt(prepend ? static_cast<int>(newIds.size()) : static_cast<int>(m_messages.size() - newIds.size()));

    if (prepend)
    {
        emit fetchedPosition(static_cast<int>(newIds.size()));
    }
}
