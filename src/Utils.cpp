#include "Utils.hpp"

#include "Chat.hpp"
#include "Emoji.hpp"
#include "Localization.hpp"
#include "Message.hpp"
#include "MessageContent.hpp"
#include "MessageService.hpp"
#include "ScopeTimer.hpp"
#include "StorageManager.hpp"
#include "User.hpp"

#include <QStringBuilder>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

// constexpr int EmojiSize18 = 18;
constexpr int EmojiSize24 = 24;
// constexpr int EmojiSize32 = 32;
// constexpr int EmojiSize48 = 48;

struct EmojiTable
{
    std::unordered_map<QString, QString> map;

    // Distinct key lengths, longest first. Probing only lengths that actually
    // exist keeps the per-position work to a handful of lookups.
    std::vector<int> keyLengthsDesc;

    // Indexed by the high byte of a UTF-16 code unit: true if any emoji key
    // starts with a unit in that block. Lets plain text skip the lookup loop.
    std::array<bool, 256> canStart{};
};

// Built on first use rather than during static initialisation. This is ~3773
// QString allocations plus the img-tag formatting; it does not belong on the
// path that runs before main().
const EmojiTable &emojiTable()
{
    static const EmojiTable table = [] {
        EmojiTable t;

        const auto &emojis = Emoji::emojis();
        t.map.reserve(emojis.size());

        std::set<int> lengths;

        for (const Emoji &emoji : emojis)
        {
            auto unicode = emoji.unicode();
            if (unicode.isEmpty())
                continue;

            lengths.insert(unicode.size());
            t.canStart[unicode.at(0).unicode() >> 8] = true;

            t.map.emplace(std::move(unicode),
                          QString("<img src=\":/emoji/%1\" width=\"%2\" height=\"%2\">").arg(emoji.filename(), QString::number(EmojiSize24)));
        }

        t.keyLengthsDesc.assign(lengths.rbegin(), lengths.rend());

        return t;
    }();

    return table;
}

}  // namespace

Utils::Utils(QObject *parent)
    : QObject(parent)
{
}

td::td_api::object_ptr<td::td_api::ChatList> Utils::toChatList(const std::unique_ptr<ChatList> &list) noexcept
{
    switch (list->type())
    {
        case ChatList::Type::Main:
            return td::td_api::make_object<td::td_api::chatListMain>();
        case ChatList::Type::Archive:
            return td::td_api::make_object<td::td_api::chatListArchive>();
        case ChatList::Type::Folder:
            return td::td_api::make_object<td::td_api::chatListFolder>(list->folderId());
        default:
            // Fallback to nullptr for None or any unexpected type
            return nullptr;
    }
}

QString Utils::formattedText(const td::td_api::object_ptr<td::td_api::formattedText> &value) noexcept
{
    if (!value)
        return {};

    const auto text = QString::fromStdString(value->text_);
    const auto &entities = value->entities_;

    QString html;
    html.reserve(int(text.length() * 1.1) + entities.size() * 10);  // Rough estimate

    int currentIndex = 0;

    auto escapeHtml = [](const QString &plain) {
        QString rich;
        rich.reserve(plain.size() * 1.1);

        for (const QChar &ch : plain)
        {
            switch (ch.unicode())
            {
                case '<':
                    rich += QLatin1String("&lt;");
                    break;
                case '>':
                    rich += QLatin1String("&gt;");
                    break;
                case '&':
                    rich += QLatin1String("&amp;");
                    break;
                case '"':
                    rich += QLatin1String("&quot;");
                    break;
                default:
                    rich += ch;
                    break;
            }
        }
        return rich;
    };

    for (const auto &entity : entities)
    {
        int offset = entity->offset_;
        int length = entity->length_;
        int entityType = entity->type_->get_id();

        if (currentIndex < offset)
        {
            html += escapeHtml(text.midRef(currentIndex, offset - currentIndex).toString());
        }

        QString entityText = escapeHtml(text.midRef(offset, length).toString());

        // Use switch to add HTML tags around entity text
        switch (entityType)
        {
            case td::td_api::textEntityTypeBold::ID:
                html += QLatin1String("<b>") % entityText % QLatin1String("</b>");
                break;
            case td::td_api::textEntityTypeItalic::ID:
                html += QLatin1String("<i>") % entityText % QLatin1String("</i>");
                break;
            case td::td_api::textEntityTypeUnderline::ID:
                html += QLatin1String("<u>") % entityText % QLatin1String("</u>");
                break;
            case td::td_api::textEntityTypeStrikethrough::ID:
                html += QLatin1String("<s>") % entityText % QLatin1String("</s>");
                break;
            case td::td_api::textEntityTypeCode::ID:
            case td::td_api::textEntityTypePre::ID:
                html += QLatin1String("<code>") % entityText % QLatin1String("</code>");
                break;
            case td::td_api::textEntityTypeTextUrl::ID: {
                auto urlEntity = static_cast<const td::td_api::textEntityTypeTextUrl *>(entity->type_.get());
                QString url = QString::fromStdString(urlEntity->url_);
                html += QLatin1String("<a href=\"") % (url.isEmpty() ? entityText : url) % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            }
            case td::td_api::textEntityTypeUrl::ID:
                html += QLatin1String("<a href=\"") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypeEmailAddress::ID:
                html += QLatin1String("<a href=\"mailto:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypePhoneNumber::ID:
                html += QLatin1String("<a href=\"tel:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypeMention::ID:
                html += QLatin1String("<a href=\"mention:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypeMentionName::ID: {
                auto mentionEntity = static_cast<const td::td_api::textEntityTypeMentionName *>(entity->type_.get());
                html += QLatin1String("<a href=\"mention_name:") % QString::number(mentionEntity->user_id_) % QLatin1String("\">") % entityText %
                        QLatin1String("</a>");
                break;
            }
            case td::td_api::textEntityTypeHashtag::ID:
                html += QLatin1String("<a href=\"hashtag:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypeCashtag::ID:
                html += QLatin1String("<a href=\"cashtag:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            case td::td_api::textEntityTypeBotCommand::ID:
                html += QLatin1String("<a href=\"botCommand:") % entityText % QLatin1String("\">") % entityText % QLatin1String("</a>");
                break;
            default:
                html += entityText;
                break;
        }

        currentIndex = offset + length;
    }

    if (currentIndex < text.length())
    {
        html += escapeHtml(text.midRef(currentIndex).toString());
    }

    return html;
}

QString Utils::formatTime(int totalSeconds) noexcept
{
    QString result;
    QTextStream stream(&result);

    auto appendDuration = [&stream](int count, QChar order) {
        if (count > 0)
        {
            if (!stream.string()->isEmpty())
            {
                stream << ' ';
            }
            stream << count << order;
        }
    };

    const int seconds = totalSeconds % 60;
    const int totalMinutes = totalSeconds / 60;
    const int minutes = totalMinutes % 60;
    const int totalHours = totalMinutes / 60;
    const int hours = totalHours % 24;
    const int days = totalHours / 24;

    appendDuration(days, 'd');
    appendDuration(hours, 'h');
    appendDuration(minutes, 'm');
    appendDuration(seconds, 's');

    return result;
}

QString Utils::replaceEmoji(const QString &text) noexcept
{
    MEEGRAM_SCOPE("Utils::replaceEmoji");

    const auto &table = emojiTable();

    // Fast path: the overwhelming majority of chat titles and messages contain no
    // emoji at all. One array index per code unit, and we hand back the original
    // string with no allocation at all.
    bool mayContainEmoji = false;
    for (int i = 0; i < text.size(); ++i)
    {
        if (table.canStart[text.at(i).unicode() >> 8])
        {
            mayContainEmoji = true;
            break;
        }
    }

    if (!mayContainEmoji)
        return text;

    QString result;
    result.reserve(text.size() + text.size() / 2);

    int lastPos = 0;
    int i = 0;

    while (i < text.size())
    {
        bool replaced = false;

        if (table.canStart[text.at(i).unicode() >> 8])
        {
            const int remaining = text.size() - i;

            // Longest match first, so a keycap sequence beats its bare base character.
            for (const int length : table.keyLengthsDesc)
            {
                if (length > remaining)
                    continue;

                if (const auto it = table.map.find(text.mid(i, length)); it != table.map.end())
                {
                    result.append(text.midRef(lastPos, i - lastPos));
                    result.append(it->second);

                    i += length;
                    lastPos = i;
                    replaced = true;
                    break;
                }
            }
        }

        if (!replaced)
        {
            ++i;
        }
    }

    result.append(text.midRef(lastPos));

    return result;
}

QString Utils::getAudioTitle(MessageAudio *audio) noexcept
{
    const auto &fileName = audio->fileName();
    const auto &title = audio->title().trimmed();
    const auto &performer = audio->performer().trimmed();

    if (title.isEmpty() && performer.isEmpty())
        return fileName;

    const auto artist = performer.isEmpty() ? QObject::tr("AudioUnknownArtist") : performer;
    const auto track = title.isEmpty() ? QObject::tr("AudioUnknownTitle") : title;

    return artist + " - " + track;
}

QString Utils::getCallContent(MessageCall *call, bool isOutgoing) noexcept
{
    const auto isVideo = call->isVideo();
    const auto isMissed = call->discardReason() == MessageCall::DiscardReason::Missed;
    const auto isBusy = call->discardReason() == MessageCall::DiscardReason::Declined;

    if (isMissed)
    {
        if (isOutgoing)
        {
            return isVideo ? QObject::tr("CallMessageVideoOutgoingMissed") : QObject::tr("CallMessageOutgoingMissed");
        }
        return isVideo ? QObject::tr("CallMessageVideoIncomingMissed") : QObject::tr("CallMessageIncomingMissed");
    }

    if (isBusy)
    {
        return isVideo ? QObject::tr("CallMessageVideoIncomingDeclined") : QObject::tr("CallMessageIncomingDeclined");
    }

    return isVideo ? (isOutgoing ? QObject::tr("CallMessageVideoOutgoing") : QObject::tr("CallMessageVideoIncoming"))
                   : (isOutgoing ? QObject::tr("CallMessageOutgoing") : QObject::tr("CallMessageIncoming"));
}

QString Utils::getMessageDate(Message *message) noexcept
{
    if (!message)
        return QString();

    const auto date = message->date();
    const auto days = date.daysTo(QDateTime::currentDateTime());

    if (days < 1)
    {
        return date.toString(tr("formatterDay12H"));
    }
    else if (days < 7)
    {
        return date.toString(tr("formatterWeek"));
    }
    else if (days < 365)
    {
        return date.toString(tr("formatterMonth"));
    }

    return date.toString(tr("formatterYear"));
}

QString Utils::getContent(Message *message, std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale) noexcept
{
    if (!message)
        return {};

    // Service messages need the whole Message, so they are resolved here rather than
    // in the content-only overload. Hoisting this out of the switch's default branch
    // is behaviour-preserving: Message::isService() matches on content type, and its
    // 43 service types are disjoint from the 16 cases the switch handles.
    if (message->isService())
    {
        return getServiceContent(message, storage, locale);
    }

    return getContent(message->content(), message->contentType(), message->isOutgoing(), std::move(locale));
}

QString Utils::getContent(MessageContent *content, int contentType, bool isOutgoing, std::shared_ptr<Locale> locale) noexcept
{
    auto formatCaption = [&](const QString &text) noexcept -> QString { return text.isEmpty() ? text : QLatin1String(": ") + text; };

    switch (contentType)
    {
        case td::td_api::messageText::ID: {
            auto text = static_cast<MessageText *>(content);
            return text->text();
        }
        case td::td_api::messageAnimation::ID: {
            auto animation = static_cast<MessageAnimation *>(content);
            return tr("AttachGif") + formatCaption(animation->caption());
        }
        case td::td_api::messageAudio::ID: {
            auto audio = static_cast<MessageAudio *>(content);
            QString title = getAudioTitle(audio);
            if (title.isEmpty())
            {
                title = tr("AttachMusic");
            }
            return title + formatCaption(audio->caption());
        }
        case td::td_api::messageDocument::ID: {
            auto document = static_cast<MessageDocument *>(content);
            if (auto fileName = document->fileName(); !fileName.isEmpty())
            {
                return fileName + formatCaption(document->caption());
            }
            return tr("AttachDocument") + formatCaption(document->caption());
        }
        case td::td_api::messagePhoto::ID: {
            auto photo = static_cast<MessagePhoto *>(content);
            return tr("AttachPhoto") + formatCaption(photo->caption());
        }
        case td::td_api::messageSticker::ID: {
            auto sticker = static_cast<MessageSticker *>(content);
            return sticker->emoji() + " " + tr("AttachSticker");
        }
        case td::td_api::messageVideo::ID: {
            auto video = static_cast<MessageVideo *>(content);
            return tr("AttachVideo") + formatCaption(video->caption());
        }
        case td::td_api::messageVideoNote::ID: {
            return tr("AttachRound");
        }
        case td::td_api::messageVoiceNote::ID: {
            auto voiceNote = static_cast<MessageVoiceNote *>(content);
            return tr("AttachAudio") + formatCaption(voiceNote->caption());
        }
        case td::td_api::messageLocation::ID: {
            return tr("AttachLocation");
        }
        case td::td_api::messageVenue::ID: {
            auto venue = static_cast<MessageVenue *>(content);
            return tr("AttachLocation") + formatCaption(venue->venue());
        }
        case td::td_api::messageContact::ID: {
            return tr("AttachContact");
        }
        case td::td_api::messageAnimatedEmoji::ID: {
            return tr("AttachSticker");
        }
        case td::td_api::messageGame::ID: {
            return tr("AttachGame");
        }
        case td::td_api::messagePoll::ID: {
            auto poll = static_cast<MessagePoll *>(content);
            return QString::fromUtf8("\xf0\x9f\x93\x8a\x20") + QLatin1String(" ") + poll->question();
        }
        case td::td_api::messageCall::ID: {
            auto call = static_cast<MessageCall *>(content);
            auto text = getCallContent(call, isOutgoing);

            if (call->duration() > 0)
            {
                const auto callDuration = locale->formatCallDuration(call->duration());
                return tr("CallMessageWithDuration").arg(text).arg(callDuration);
            }

            return text;
        }
        default:
            // Service messages were handled by the Message* overload before we got
            // here, so anything reaching this point really is unsupported.
            return tr("UnsupportedAttachment");
    }
}

QString Utils::getServiceContent(Message *message, std::shared_ptr<StorageManager> storage, std::shared_ptr<Locale> locale, bool openUser) noexcept
{
    if (!message)
        return {};

    auto content = message->content();
    auto contentType = message->contentType();
    auto isOutgoing = message->isOutgoing();

    auto chat = storage->chat(message->chatId());
    if (!chat)
        return {};

    const auto author = getSenderAuthor(message, storage, openUser);
    const bool isChannel = chat->type() == Chat::Type::Channel;

    auto service = static_cast<MessageService *>(content);

    switch (contentType)
    {
        case td::td_api::messageExpiredPhoto::ID: {
            return tr("AttachPhotoExpired");
        }
        case td::td_api::messageExpiredVideo::ID: {
            return tr("AttachVideoExpired");
        }
        case td::td_api::messageBasicGroupChatCreate::ID: {
            return isOutgoing ? tr("ActionYouCreateGroup") : tr("ActionCreateGroup").replace("un1", author);
        }
        case td::td_api::messageSupergroupChatCreate::ID: {
            return isChannel ? tr("ActionCreateChannel") : tr("ActionCreateMega");
        }
        case td::td_api::messageChatChangeTitle::ID: {
            return isChannel ? tr("ActionChannelChangedTitle").replace("un2", service->groupTitle())
                             : (isOutgoing ? tr("ActionYouChangedTitle").replace("un2", service->groupTitle())
                                           : tr("ActionChangedTitle").replace("un1", author).replace("un2", service->groupTitle()));
        }
        case td::td_api::messageChatChangePhoto::ID: {
            return isChannel ? tr("ActionChannelChangedPhoto") : (isOutgoing ? tr("ActionYouChangedPhoto") : tr("ActionChangedPhoto").replace("un1", author));
        }
        case td::td_api::messageChatDeletePhoto::ID: {
            return isChannel ? tr("ActionChannelRemovedPhoto") : (isOutgoing ? tr("ActionYouRemovedPhoto") : tr("ActionRemovedPhoto").replace("un1", author));
        }
        case td::td_api::messageChatAddMembers::ID: {
            const bool singleMember = service->addedMembers().size() == 1;
            if (singleMember)
            {
                bool ok = false;
                const auto memberUserId = service->addedMembers().front().toLongLong(&ok);
                if (!ok)
                    return {};

                if (message->senderId() == memberUserId)
                {
                    if (chat->type() == Chat::Type::Supergroup)
                    {
                        return isChannel ? tr("ChannelJoined")
                                         : (Utils::isMeUser(storage->user(memberUserId), storage) ? tr("ChannelMegaJoined")
                                                                                                  : tr("ActionAddUserSelfMega").replace("un1", author));
                    }
                    return isOutgoing ? tr("ActionAddUserSelfYou") : tr("ActionAddUserSelf").replace("un1", author);
                }
                return isOutgoing
                           ? tr("ActionYouAddUser").replace("un2", Utils::getUserName(storage->user(memberUserId), openUser))
                           : (Utils::isMeUser(storage->user(memberUserId), storage)
                                  ? (isChannel ? tr("ChannelAddedBy").replace("un1", author) : tr("MegaAddedBy").replace("un1", author))
                                  : tr("ActionAddUser").replace("un1", author).replace("un2", Utils::getUserName(storage->user(memberUserId), openUser)));
            }

            QStringList userList;
            std::ranges::for_each(service->addedMembers(), [&](const auto &userId) {
                bool ok = false;
                if (auto id = userId.toLongLong(&ok); ok)
                {
                    userList.append(Utils::getUserName(storage->user(id), openUser));
                }
            });

            const QString users = userList.join(", ");
            return isOutgoing ? tr("ActionYouAddUser").replace("un1", users) : tr("ActionAddUser").replace("un1", author).replace("un2", users);
        }
        case td::td_api::messageChatJoinByLink::ID: {
            return isOutgoing ? tr("ActionInviteYou") : tr("ActionInviteUser").replace("un1", author);
        }
        case td::td_api::messageChatDeleteMember::ID: {
            if (service->removedMember() == message->senderId())
            {
                return isOutgoing ? tr("ActionYouLeftUser") : tr("ActionLeftUser").replace("un1", author);
            }
            return isOutgoing ? tr("ActionYouKickUser").replace("un2", Utils::getUserName(storage->user(service->removedMember()), openUser))
                              : (Utils::isMeUser(storage->user(service->removedMember()), storage)
                                     ? tr("ActionKickUserYou").replace("un1", author)
                                     : tr("ActionKickUser")
                                           .replace("un1", author)
                                           .replace("un2", Utils::getUserName(storage->user(service->removedMember()), openUser)));
        }
        case td::td_api::messageChatUpgradeTo::ID:
        case td::td_api::messageChatUpgradeFrom::ID: {
            return tr("ActionMigrateFromGroup");
        }
        case td::td_api::messagePinMessage::ID: {
            return tr("ActionPinned").replace("un1", author);
        }
        case td::td_api::messageScreenshotTaken::ID: {
            return isOutgoing ? tr("ActionTakeScreenshootYou") : tr("ActionTakeScreenshoot").replace("un1", author);
        }
        case td::td_api::messageChatSetMessageAutoDeleteTime::ID: {
            const auto autoDeleteTime = service->autoDeleteTime();
            const auto ttlString = locale->formatTtl(autoDeleteTime);

            QString actionKey = isOutgoing ? (autoDeleteTime <= 0 ? tr("ActionTTLYouDisabled") : tr("ActionTTLYouChanged").arg(ttlString))
                                           : (autoDeleteTime <= 0 ? tr("ActionTTLDisabled") : tr("ActionTTLChanged").arg(ttlString));

            return actionKey.replace("un1", author);
        }
        case td::td_api::messageCustomServiceAction::ID: {
            return service->customAction();
        }
        case td::td_api::messageContactRegistered::ID: {
            return tr("NotificationContactJoined").arg(Utils::getUserName(storage->user(message->senderId()), openUser));
        }
        default: {
            return tr("UnsupportedMedia");
        }
    }
}

QString Utils::getChatTitle(std::shared_ptr<Chat> chat, std::shared_ptr<StorageManager> storage, bool showSavedMessages) noexcept
{
    if (isMeChat(chat, storage) && showSavedMessages)
    {
        return QObject::tr("SavedMessages");
    }

    const auto title = chat->title().trimmed();

    return !title.isEmpty() ? title : QObject::tr("HiddenName");
}

QString Utils::getSenderName(Message *message, std::shared_ptr<StorageManager> storage) noexcept
{
    switch (message->senderType())
    {
        case Message::SenderType::User: {
            auto user = storage->user(message->senderId());
            return Utils::getUserShortName(user);
        }
        case Message::SenderType::Chat: {
            auto chat = storage->chat(message->senderId());
            return getChatTitle(chat, storage, false);
        }
        default:
            return {};
    }
}

QString Utils::getSenderAuthor(Message *message, std::shared_ptr<StorageManager> storage, bool openUser) noexcept
{
    const QString linkStyle = QLatin1String("<a style=\"text-decoration: none; font-weight: bold; color: darkgray\" href=\"");
    const QString linkClose = QLatin1String("</a>");

    switch (message->senderType())
    {
        case Message::SenderType::User: {
            auto user = storage->user(message->senderId());
            const auto userName = Utils::getUserShortName(user);

            if (openUser)
            {
                return linkStyle + QLatin1String("userId://") + QString::number(message->senderId()) + QLatin1String("\">") + userName + linkClose;
            }

            return userName;
        }
        case Message::SenderType::Chat: {
            auto chat = storage->chat(message->senderId());
            const auto chatTitle = getChatTitle(chat, storage, false);

            if (openUser)
            {
                return linkStyle + QLatin1String("chatId://") + QString::number(message->senderId()) + QLatin1String("\">") + chatTitle + linkClose;
            }
            return chatTitle;
        }
        default:
            return {};
    }
}

QString Utils::getUserName(std::shared_ptr<User> user, bool openUser) noexcept
{
    const auto userName = getUserShortName(user);

    if (userName.isEmpty())
    {
        return QString();
    }

    if (openUser)
    {
        const QString linkStart = QLatin1String("<a style=\"text-decoration: none; font-weight: bold; color: darkgray\" href=\"userId://");
        const QString linkEnd = QLatin1String("</a>");
        return linkStart + QString::number(user->id()) + QLatin1String("\">") + userName + linkEnd;
    }

    return userName;
}

QString Utils::getUserShortName(std::shared_ptr<User> user) noexcept
{
    if (!user)
        return QString();

    switch (user->type())
    {
        case User::Type::Bot:
        case User::Type::Regular: {
            if (!user->firstName().isEmpty())
            {
                return user->firstName();
            }
            if (!user->lastName().isEmpty())
            {
                return user->lastName();
            }
            break;
        }
        case User::Type::Deleted:
        case User::Type::Unknown:
            return tr("HiddenName");
        default:
            return QString();
    }

    return QString();
}

QString Utils::getUserFullName(std::shared_ptr<User> user) noexcept
{
    if (!user)
        return QString();

    switch (user->type())
    {
        case User::Type::Bot:
        case User::Type::Regular:
            return QString(user->firstName() + " " + user->lastName()).trimmed();

        case User::Type::Deleted:
        case User::Type::Unknown:
            return tr("HiddenName");

        default:
            return QString();
    }
}

bool Utils::isMeUser(std::shared_ptr<User> user, std::shared_ptr<StorageManager> storage) noexcept
{
    return storage->myId() == user->id();
}

bool Utils::isMeChat(std::shared_ptr<Chat> chat, std::shared_ptr<StorageManager> storage) noexcept
{
    if (!chat)  // Safety check for null pointers
        return false;

    const auto chatType = chat->type();
    const auto myId = storage->myId();

    switch (chatType)
    {
        case Chat::Type::Secret:
            return myId == chat->typeId();

        case Chat::Type::Private:
            return myId == chat->typeId();

        default:
            return {};
    }

    return false;
}
