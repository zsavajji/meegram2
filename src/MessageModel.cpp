#include "MessageModel.hpp"

#include "ChatManager.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "MessageService.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QDateTime>
#include <QDebug>
#include <QImageReader>
#include <QLocale>

#include <algorithm>
#include <ranges>

MessageModel::MessageModel(std::shared_ptr<Chat> chat, std::shared_ptr<Locale> locale, std::shared_ptr<StorageManager> storage)
    : m_client(storage->client())
    , m_locale(std::move(locale))
    , m_storage(std::move(storage))
    , m_chat(std::move(chat))
{
    qDebug() << "MessageModel initialized.";

    connect(m_chat.get(), SIGNAL(chatChanged()), SLOT(handleChatItem()));
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
        case SenderRole:
            return Utils::getSenderName(message.get(), m_storage);
        case ChatIdRole:
            return message->chatId();
        case IsOutgoingRole:
            return message->isOutgoing();
        case DateRole:
            return message->date().toString(QObject::tr("formatterDay12H"));
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
        case ContentTypeRole:
            return message->contentTypeString();
        case IsServiceRole:
            return message->isService();
        case ServiceMessageRole:
            return Utils::getServiceContent(message.get(), m_storage, m_locale, true);
        case SectionRole: {
            static const auto currentDateTime = QDateTime::currentDateTime();
            const auto days = message->date().daysTo(currentDateTime);

            if (days == 0)
                return QObject::tr("MessageScheduleToday");
            else if (days == 1)
                return QObject::tr("Yesterday");
            else
                return message->date().toString(QObject::tr("chatFullDate"));
        }
        case ReplyToSenderRole:
            return replyToSender(message.get());
        case ReplyToTextRole:
            return replyToText(message.get());
    }

    return QVariant();
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

QHash<int, QByteArray> MessageModel::roleNames() const noexcept
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[SenderRole] = "sender";
    roles[ChatIdRole] = "chatId";
    roles[IsOutgoingRole] = "isOutgoing";
    roles[DateRole] = "date";
    roles[EditDateRole] = "editDate";
    roles[ContentRole] = "content";
    // Custom
    roles[ContentTypeRole] = "contentType";
    roles[IsServiceRole] = "isService";
    roles[ServiceMessageRole] = "serviceMessage";
    roles[SectionRole] = "section";
    roles[ReplyToSenderRole] = "replyToSender";
    roles[ReplyToTextRole] = "replyToText";
    return roles;
}

int MessageModel::count() const noexcept
{
    return m_messages.size();
}

bool MessageModel::backFetching() const noexcept
{
    return m_backFetching;
}

bool MessageModel::loading() const noexcept
{
    return m_loading;
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

    // Logged on the way out as well as on the way back, so an empty log distinguishes
    // "never asked" from "asked and got nothing". qWarning survives release builds;
    // reading either needs the binary run directly, since invoker discards stderr.
    qWarning() << "getChatHistory ->" << "chat" << m_chat->id() << "from" << fromMessageId << "offset" << offset << "limit" << limit;

    m_client->send(std::move(request), [this, fetchPrevious, alive = m_alive](auto &&response) {
        // Runs on the TDLib worker thread, and the model may already be gone: leaving a
        // chat destroys it, and a history request is usually still outstanding when you
        // do. Everything below touches members or emits, so bail before any of it.
        if (!alive->load())
            return;

        auto cleanupFlags = [this]() {
            if (m_loading)
            {
                m_loading = false;
                emit loadingChanged();
            }
            if (m_backFetching)
            {
                m_backFetching = false;
                emit backFetchingChanged();
            }

            emit countChanged();
        };

        if (response->get_id() != td::td_api::messages::ID)
        {
            // qWarning, not qDebug: release builds define QT_NO_DEBUG_OUTPUT, so a
            // rejected request left no trace at all and the chat just span forever.
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
            // An empty answer does not mean the chat is empty. TDLib may return fewer
            // messages than asked for - or none - while its own fetch is still in
            // flight, and expects the request to be repeated; for a supergroup there is
            // often nothing cached locally to answer from on the first call. Treating
            // empty as final is what leaves a group on a spinner forever.
            //
            // Queued, and via the timer's own start slot: this runs on the TDLib worker
            // thread, and a QTimer may only be started from the thread that owns it.
            // m_loading stays set, so the view keeps showing it is still working.
            QMetaObject::invokeMethod(&m_historyRetryTimer, "start", Qt::QueuedConnection);
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

        // Not called inline: this whole callback runs on the TDLib worker thread, and
        // linkContentFile reaches into StorageManager's file map, which the main thread
        // mutates on every updateFile. Two threads doing try_emplace on one
        // unordered_map can leave a reader walking a broken bucket chain forever.
        QMetaObject::invokeMethod(this, "linkLoadedContentFiles", Qt::QueuedConnection);

        cleanupFlags();
    });
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

void MessageModel::sendMessage(const QString &message, const QString &replyToMessageId) noexcept
{
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();

    content->text_ = td::td_api::make_object<td::td_api::formattedText>();
    content->text_->text_ = message.toStdString();

    send(std::move(content), toId(replyToMessageId));
}

void MessageModel::sendPhoto(const QString &filePath, const QString &caption, const QString &replyToMessageId) noexcept
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

    send(std::move(content), toId(replyToMessageId));
}

void MessageModel::fetchMoreBack() noexcept
{
    if (m_backFetching || m_messages.empty())
        return;

    m_backFetching = true;

    requestHistory(std::ranges::min(m_messages), 0, MessageSliceLimit, true);

    emit backFetchingChanged();
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
    endResetModel();

    emit countChanged();
}

void MessageModel::handleChatItem() noexcept
{
    qDebug() << "Title" << m_chat->title();
    qDebug() << "Unread count" << m_chat->unreadCount();
    qDebug() << "Last read inbox message id" << m_chat->lastReadInboxMessageId();
    qDebug() << "Last read outbox message id" << m_chat->lastReadOutboxMessageId();
    qDebug() << "Unread mention count" << m_chat->unreadMentionCount();
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

    emit countChanged();

    // Messages sort by id and a new one always has the highest, so this is the last
    // row - but say so explicitly rather than leave the view to assume it.
    if (pos == static_cast<int>(m_messages.size()) - 1)
    {
        emit messageAppended();
    }
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

        itemChanged(std::distance(m_messages.begin(), std::ranges::find(m_messages, messageId)));
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

void MessageModel::handleDeleteMessages(qlonglong chatId, std::vector<int64_t> &&messageIds, bool isPermanent, bool fromCache) noexcept
{
    Q_UNUSED(isPermanent)
    Q_UNUSED(fromCache)

    if (chatId != m_chat->id())
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

    endRemoveRows();
}

void MessageModel::reloadHistory() noexcept
{
    if (m_historyRetries >= MaxHistoryRetries)
    {
        qWarning() << "getChatHistory: gave up after" << m_historyRetries << "empty replies for chat" << m_chat->id();

        if (m_loading)
        {
            m_loading = false;
            emit loadingChanged();
        }

        if (m_backFetching)
        {
            m_backFetching = false;
            emit backFetchingChanged();
        }

        emit countChanged();
        return;
    }

    ++m_historyRetries;

    loadMessages();
}

void MessageModel::loadMessages() noexcept
{
    // Always the newest slice. This used to anchor on lastReadInboxMessageId with a
    // negative offset when the chat had unread messages, so opening a chat with more
    // unread than one slice loaded a window that did not contain the newest message at
    // all - the bottom of the list was not the last message received.
    //
    // A chat can have no last message - freshly created, or its history cleared - and
    // this dereferenced it unconditionally. from_message_id 0 means "from the newest",
    // which is what is wanted anyway.
    const auto *lastMessage = m_chat->lastMessage();

    requestHistory(lastMessage ? lastMessage->id() : 0, 0, MessageSliceLimit);
}

int MessageModel::lastMessageIndex() const noexcept
{
    // find() returns end() - i.e. count() - when the last read message is not in the
    // loaded slice, which is exactly when the view must fall back to the newest
    // message rather than anchor on whatever happens to be loaded.
    return std::distance(m_messages.begin(), std::ranges::find(m_messages, m_chat->lastReadInboxMessageId()));
}

void MessageModel::itemChanged(size_t index) noexcept
{
    QModelIndex modelIndex = createIndex(static_cast<int>(index), 0);

    emit dataChanged(modelIndex, modelIndex);
}

void MessageModel::insertMessages(std::vector<qlonglong> &&newIds, bool prepend)
{
    if (newIds.empty())
        return;

    std::ranges::sort(newIds);

    if (prepend)
    {
        auto pos = static_cast<int>(newIds.size());
        beginInsertRows(QModelIndex(), 0, pos - 1);
        m_messages.insert(m_messages.begin(), newIds.begin(), newIds.end());
    }
    else
    {
        auto pos = static_cast<int>(m_messages.size());
        beginInsertRows(QModelIndex(), pos, pos + static_cast<int>(newIds.size()) - 1);
        m_messages.insert(m_messages.end(), newIds.begin(), newIds.end());
    }

    endInsertRows();

    auto mid = m_messages.begin() + (prepend ? newIds.size() : (m_messages.size() - newIds.size()));
    std::ranges::inplace_merge(m_messages.begin(), mid, m_messages.end());

    if (prepend)
    {
        emit fetchedPosition(static_cast<int>(newIds.size()));
    }
}
