#include "StorageManager.hpp"

#include "Utils.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_set>

StorageManager::StorageManager(std::shared_ptr<Client> client, QObject *parent)
    : QObject(parent)
    , m_client(std::move(client))
{
    connect(m_client.get(), SIGNAL(result(td::td_api::Object *)), SLOT(handleResult(td::td_api::Object *)));
}

std::shared_ptr<Client> StorageManager::client() const noexcept
{
    return m_client;
}

std::vector<qlonglong> StorageManager::chatIds() const noexcept
{
    auto view = m_chats | std::views::keys;
    return std::vector(view.begin(), view.end());
}

std::vector<std::shared_ptr<ChatFolderInfo>> StorageManager::chatFolders() const noexcept
{
    return m_chatFolders;
}

std::shared_ptr<BasicGroup> StorageManager::basicGroup(qlonglong groupId) const noexcept
{
    if (auto it = m_basicGroup.find(groupId); it != m_basicGroup.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Chat> StorageManager::chat(qlonglong chatId) const noexcept
{
    if (auto it = m_chats.find(chatId); it != m_chats.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<File> StorageManager::registerFile(std::shared_ptr<File> file) noexcept
{
    if (!file)
        return {};

    // First registrant wins. An instance already in the map is one some other holder
    // may be bound to, and it is never staler: updateFile carries a full td_api::file
    // and is sent for every state change.
    const auto [it, inserted] = m_files.try_emplace(file->id(), std::move(file));

    return it->second;
}

void StorageManager::registerChatPhoto(const std::shared_ptr<Chat> &chat) noexcept
{
    if (const auto &file = chat->photoFile())
    {
        chat->adoptPhotoFile(registerFile(file));
    }

    if (const auto &file = chat->bigPhotoFile())
    {
        chat->adoptBigPhotoFile(registerFile(file));
    }
}

std::shared_ptr<File> StorageManager::file(int fileId) const noexcept
{
    if (auto it = m_files.find(fileId); it != m_files.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Supergroup> StorageManager::supergroup(qlonglong groupId) const noexcept
{
    if (auto it = m_supergroup.find(groupId); it != m_supergroup.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<SupergroupFullInfo> StorageManager::supergroupFullInfo(qlonglong groupId) const noexcept
{
    if (auto it = m_supergroupFullInfo.find(groupId); it != m_supergroupFullInfo.end())
    {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<User> StorageManager::user(qlonglong userId) const noexcept
{
    if (auto it = m_users.find(userId); it != m_users.end())
    {
        return it->second;
    }

    return nullptr;
}

QString StorageManager::userBio(qlonglong userId) const noexcept
{
    if (auto it = m_userBios.find(userId); it != m_userBios.end())
    {
        return it->second;
    }

    return {};
}

void StorageManager::loadUserFullInfo(qlonglong userId) noexcept
{
    m_client->send(td::td_api::make_object<td::td_api::getUserFullInfo>(userId), [this, userId](auto &&response) {
        // Runs on the TDLib worker thread. Nothing but the response is touched here;
        // the map and the signal are left to the queued call below.
        if (response->get_id() != td::td_api::userFullInfo::ID)
            return;

        const auto *fullInfo = static_cast<const td::td_api::userFullInfo *>(response.get());

        QMetaObject::invokeMethod(this, "setUserBio", Qt::QueuedConnection, Q_ARG(qlonglong, userId),
                                  Q_ARG(QString, Utils::formattedText(fullInfo->bio_)));
    });
}

void StorageManager::setUserBio(qlonglong userId, const QString &bio) noexcept
{
    m_userBios.insert_or_assign(userId, bio);

    emit userFullInfoUpdated(userId);
}

qlonglong StorageManager::myId() const noexcept
{
    if (m_myId != 0)
        return m_myId;

    if (const auto value = getOption("my_id"); not value.isNull())
        m_myId = value.toLongLong();

    return m_myId;
}

QVariant StorageManager::getOption(const QString &name) const noexcept
{
    if (auto it = m_options.find(name); it != m_options.end())
        return it.value();

    return QVariant();
}

void StorageManager::handleResult(td::td_api::Object *object)
{
    switch (object->get_id())
    {
        case td::td_api::updateNewChat::ID: {
            auto update = static_cast<td::td_api::updateNewChat *>(object);
            auto chat = std::make_shared<Chat>(std::move(update->chat_));
            auto chatId = chat->id();
            registerChatPhoto(chat);
            m_chats.emplace(chatId, std::move(chat));
            emit chatUpdated(chatId);
            break;
        }
        case td::td_api::updateChatTitle::ID: {
            auto update = static_cast<td::td_api::updateChatTitle *>(object);
            auto chatId = update->chat_id_;
            if (auto it = m_chats.find(chatId); it != m_chats.end())
            {
                it->second->setTitle(update->title_);
                emit chatUpdated(chatId);
            }
            break;
        }
        case td::td_api::updateChatPhoto::ID: {
            auto update = static_cast<td::td_api::updateChatPhoto *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setPhoto(std::move(update->photo_));
                registerChatPhoto(it->second);
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatLastMessage::ID: {
            auto update = static_cast<td::td_api::updateChatLastMessage *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setLastMessage(std::move(update->last_message_));
                it->second->setPositions(std::move(update->positions_));
                emit chatUpdated(update->chat_id_);
                // The only site that may raise a notification. Every other emit of
                // chatUpdated is a chat that changed or merely arrived, not a message.
                emit chatLastMessageChanged(update->chat_id_);
                emit chatPositionUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatAction::ID: {
            auto update = static_cast<td::td_api::updateChatAction *>(object);

            // Only the sender id is kept: turning it into a name needs a Locale, which
            // lives with the formatter that displays this.
            qlonglong senderId = 0;

            if (update->sender_id_ && update->sender_id_->get_id() == td::td_api::messageSenderUser::ID)
            {
                senderId = static_cast<const td::td_api::messageSenderUser *>(update->sender_id_.get())->user_id_;
            }

            const auto cancelled = update->action_ && update->action_->get_id() == td::td_api::chatActionCancel::ID;

            emit chatActionUpdated(update->chat_id_, senderId, cancelled ? 0 : update->action_->get_id());
            break;
        }
        case td::td_api::updateChatPosition::ID: {
            auto update = static_cast<td::td_api::updateChatPosition *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                std::vector<td::td_api::object_ptr<td::td_api::chatPosition>> result;
                result.emplace_back(std::move(update->position_));
                it->second->setPositions(std::move(result));
                emit chatUpdated(update->chat_id_);
                emit chatPositionUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatReadInbox::ID: {
            auto update = static_cast<td::td_api::updateChatReadInbox *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setUnreadCount(update->unread_count_);
                it->second->setLastReadInboxMessageId(update->last_read_inbox_message_id_);
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatReadOutbox::ID: {
            auto update = static_cast<td::td_api::updateChatReadOutbox *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setLastReadOutboxMessageId(update->last_read_outbox_message_id_);
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatDraftMessage::ID: {
            auto update = static_cast<td::td_api::updateChatDraftMessage *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setPositions(std::move(update->positions_));
                emit chatUpdated(update->chat_id_);
                emit chatPositionUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatNotificationSettings::ID: {
            auto update = static_cast<td::td_api::updateChatNotificationSettings *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setNotificationSettings(std::move(update->notification_settings_));
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatUnreadMentionCount::ID: {
            auto update = static_cast<td::td_api::updateChatUnreadMentionCount *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setUnreadMentionCount(update->unread_mention_count_);
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatIsMarkedAsUnread::ID: {
            auto update = static_cast<td::td_api::updateChatIsMarkedAsUnread *>(object);
            if (auto it = m_chats.find(update->chat_id_); it != m_chats.end())
            {
                it->second->setIsMarkedAsUnread(update->is_marked_as_unread_);
                emit chatUpdated(update->chat_id_);
            }
            break;
        }
        case td::td_api::updateChatOnlineMemberCount::ID: {
            auto update = static_cast<td::td_api::updateChatOnlineMemberCount *>(object);
            emit chatOnlineMemberCountUpdated(update->chat_id_, update->online_member_count_);
            break;
        }
        case td::td_api::updateUser::ID: {
            auto update = static_cast<td::td_api::updateUser *>(object);
            auto user = std::make_shared<User>(std::move(update->user_));
            auto userId = user->id();
            m_users.insert_or_assign(userId, std::move(user));
            emit userUpdated(userId);
            break;
        }
        case td::td_api::updateUserStatus::ID: {
            auto update = static_cast<td::td_api::updateUserStatus *>(object);
            auto userId = update->user_id_;
            if (auto it = m_users.find(userId); it != m_users.end())
            {
                it->second->setStatus(std::move(update->status_));
                emit userUpdated(userId);
            }
            break;
        }
        case td::td_api::updateUserFullInfo::ID: {
            // Keeps a bio already on screen current. The profile page does not wait for
            // this - see loadUserFullInfo for why it cannot.
            auto update = static_cast<td::td_api::updateUserFullInfo *>(object);
            setUserBio(update->user_id_, Utils::formattedText(update->user_full_info_->bio_));
            break;
        }
        case td::td_api::updateBasicGroup::ID: {
            auto update = static_cast<td::td_api::updateBasicGroup *>(object);
            auto group = std::make_shared<BasicGroup>(std::move(update->basic_group_));
            auto groupId = group->id();
            m_basicGroup.insert_or_assign(groupId, std::move(group));
            emit basicGroupUpdated(groupId);
            break;
        }
        case td::td_api::updateSupergroup::ID: {
            auto update = static_cast<td::td_api::updateSupergroup *>(object);
            auto supergroup = std::make_shared<Supergroup>(std::move(update->supergroup_));
            auto groupId = supergroup->id();
            m_supergroup.insert_or_assign(groupId, std::move(supergroup));
            emit supergroupUpdated(groupId);
            break;
        }
        case td::td_api::updateSupergroupFullInfo::ID: {
            auto update = static_cast<td::td_api::updateSupergroupFullInfo *>(object);
            auto supergroupId = update->supergroup_id_;
            auto supergroupFullInfo = std::make_shared<SupergroupFullInfo>(std::move(update->supergroup_full_info_));
            m_supergroupFullInfo.insert_or_assign(supergroupId, std::move(supergroupFullInfo));
            break;
        }
        case td::td_api::updateChatFolders::ID: {
            auto update = static_cast<td::td_api::updateChatFolders *>(object);
            m_chatFolders.clear();
            m_chatFolders.reserve(update->chat_folders_.size());
            std::ranges::transform(update->chat_folders_, std::back_inserter(m_chatFolders),
                                   [](auto &&folder) { return std::make_shared<ChatFolderInfo>(std::move(folder)); });

            emit chatFoldersUpdated();
            break;
        }
        case td::td_api::updateFile::ID: {
            auto update = static_cast<td::td_api::updateFile *>(object);
            auto fileId = update->file_->id_;
            if (auto it = m_files.find(fileId); it != m_files.end())
            {
                it->second->setFile(std::move(update->file_));
            }
            else
            {
                m_files[fileId] = std::make_shared<File>(std::move(update->file_));
            }
            break;
        }
        case td::td_api::updateOption::ID: {
            auto update = static_cast<td::td_api::updateOption *>(object);
            auto optionValue = [](td::td_api::object_ptr<td::td_api::OptionValue> &&option) -> QVariant {
                switch (option->get_id())
                {
                    case td::td_api::optionValueBoolean::ID:
                        return QVariant::fromValue(static_cast<td::td_api::optionValueBoolean *>(option.get())->value_);
                    case td::td_api::optionValueInteger::ID:
                        return QVariant::fromValue(static_cast<td::td_api::optionValueInteger *>(option.get())->value_);
                    case td::td_api::optionValueString::ID:
                        return QVariant::fromValue(QString::fromStdString(static_cast<td::td_api::optionValueString *>(option.get())->value_));
                    default:
                        return QVariant();
                }
            };
            m_options.insert(QString::fromStdString(update->name_), optionValue(std::move(update->value_)));
            m_myId = 0;  // invalidate the myId() cache; cheap, options change rarely
            break;
        }
        default:
            break;
    }
}
