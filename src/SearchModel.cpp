#include "SearchModel.hpp"

#include "Chat.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "File.hpp"
#include "StorageManager.hpp"
#include "User.hpp"
#include "Utils.hpp"

#include <QDebug>
#include <QStringList>

// QtMobility 1.2, which is what Harmattan ships. Optional because a desktop Qt 4 usually
// has no QtContacts at all - see the find_path block in CMakeLists.txt - and the rest of
// the app has no business failing to build over the address book.
#ifdef MEEGRAM_PHONE_CONTACTS
#include <QContactFetchHint>
#include <QContactManager>
#include <QContactName>
#include <QContactPhoneNumber>

QTM_USE_NAMESPACE
#endif

namespace {

// The phone's own address book, as the contacts TDLib wants to be handed. Every number a
// person has is sent, not just the first: which one they registered with is exactly what
// we do not know. Names travel with them because that is what the account will show for
// anybody who has not set their own.
std::vector<td::td_api::object_ptr<td::td_api::importedContact>> phoneBookContacts() noexcept
{
    std::vector<td::td_api::object_ptr<td::td_api::importedContact>> contacts;

#ifdef MEEGRAM_PHONE_CONTACTS
    // ponytail: synchronous, on the UI thread. The hint holds it to two details per
    // person and no photos, which is a fraction of a second for an ordinary address
    // book on this device - and it runs once, behind a confirmation, not on a path
    // anybody scrolls. QContactFetchRequest is the way out if a big book stutters.
    QContactFetchHint hint;
    hint.setDetailDefinitionsHint(QStringList() << QContactName::DefinitionName << QContactPhoneNumber::DefinitionName);
    hint.setOptimizationHints(QContactFetchHint::NoRelationships | QContactFetchHint::NoActionPreferences | QContactFetchHint::NoBinaryBlobs);

    // The default backend, which on Harmattan is tracker - the same store the People
    // application shows.
    QContactManager manager;

    const auto book = manager.contacts(QList<QContactSortOrder>(), hint);

    for (const auto &entry : book)
    {
        const auto name = entry.detail<QContactName>();

        for (const auto &phoneNumber : entry.details<QContactPhoneNumber>())
        {
            const auto number = phoneNumber.number().trimmed();

            if (number.isEmpty())
                continue;

            contacts.push_back(td::td_api::make_object<td::td_api::importedContact>(number.toStdString(), name.firstName().toStdString(),
                                                                                   name.lastName().toStdString(), nullptr));

            // ponytail: a hard ceiling rather than paging. No real address book comes
            // near it, and an unbounded request is one TDLib can reject outright.
            if (contacts.size() >= MaxImportedContacts)
                return contacts;
        }
    }

    if (manager.error() != QContactManager::NoError)
        qWarning() << "reading the address book failed:" << manager.error();
#endif

    return contacts;
}

}  // namespace

SearchModel::SearchModel(std::shared_ptr<StorageManager> storage)
    : m_storage(std::move(storage))
{
    setRoleNames(roleNames());
}

SearchModel::~SearchModel()
{
    // Tell any in-flight search callback not to touch this object.
    m_alive->store(false);
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return static_cast<int>(m_rows.size());
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto &row = m_rows[index.row()];

    switch (role)
    {
        case IdRole:
            // A decimal string, like every id crossing into QML - see Common.hpp.
            return QString::number(row.id);
        case TitleRole:
            return row.chat ? Utils::getChatTitle(row.chat, m_storage) : Utils::getUserName(row.user, false);
        case UsernameRole:
            return row.chat ? Utils::getChatUsername(row.chat, m_storage) : Utils::getUserUsername(row.user);
        case PhotoRole:
            // Returned fresh per call rather than held anywhere: the File belongs to the
            // Chat or the User, both of which this row keeps alive.
            return QVariant::fromValue(row.chat ? row.chat->photo() : row.user->photo());
        default:
            return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[IdRole] = "id";
    roles[TitleRole] = "title";
    roles[UsernameRole] = "username";
    roles[PhotoRole] = "photo";

    return roles;
}

int SearchModel::count() const noexcept
{
    return static_cast<int>(m_rows.size());
}

bool SearchModel::loading() const noexcept
{
    return m_loading;
}

bool SearchModel::searched() const noexcept
{
    return m_searched;
}

int SearchModel::beginRequest(int pendingResponses)
{
    beginResetModel();
    m_rows.clear();
    m_seenIds.clear();
    endResetModel();

    emit countChanged();

    if (m_searched)
    {
        m_searched = false;
        emit searchedChanged();
    }

    m_pending = pendingResponses;

    return ++m_requestId;
}

void SearchModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;

    m_loading = loading;
    emit loadingChanged();
}

void SearchModel::search(const QString &query)
{
    auto trimmed = query.trimmed();

    // TDLib matches on the bare username, so a typed or pasted "@name" would find nothing.
    if (trimmed.startsWith(QLatin1Char('@')))
        trimmed.remove(0, 1);

    const auto requestId = beginRequest(2);

    auto client = m_storage->client();

    // An empty field is not a query - TDLib answers it with an error, and the caller wants
    // the model cleared anyway, which beginRequest has already done.
    if (trimmed.isEmpty() || !client)
    {
        m_pending = 0;
        setLoading(false);
        return;
    }

    setLoading(true);

    // Answers arrive on the TDLib worker thread. Hopping to the main thread before
    // touching the model is the same discipline ChatModel's loadChats callback follows,
    // including that updateNewChat and updateUser for these have already been processed by
    // the time the queued call is delivered.
    const auto collect = [this, requestId, alive = m_alive](const char *what, auto &&response, const auto &idsOf) {
        if (!alive->load())
            return;

        QVariantList ids;

        if (response->get_id() == td::td_api::error::ID)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());
            qWarning() << what << "failed:" << error->code_ << QString::fromStdString(error->message_);
        }
        else
        {
            for (auto id : idsOf(response))
            {
                ids.append(QString::number(id));
            }
        }

        QMetaObject::invokeMethod(this, "handleResults", Qt::QueuedConnection, Q_ARG(int, requestId), Q_ARG(QVariantList, ids));
    };

    // People you know, matched on first name, last name or username. This is the half that
    // answers offline, and the half that finds somebody you have saved but never written
    // to - a user id is also the id of the private chat with them, so both halves of the
    // search speak in chat ids from here on.
    client->send(td::td_api::make_object<td::td_api::searchContacts>(trimmed.toStdString(), ContactSearchLimit), [collect](auto &&response) {
        collect("searchContacts", response,
                [](const auto &r) { return r->get_id() == td::td_api::users::ID ? static_cast<const td::td_api::users *>(r.get())->user_ids_ : std::vector<std::int64_t>(); });
    });

    // Everyone else: public usernames, server side.
    client->send(td::td_api::make_object<td::td_api::searchPublicChats>(trimmed.toStdString(), nullptr), [collect](auto &&response) {
        collect("searchPublicChats", response,
                [](const auto &r) { return r->get_id() == td::td_api::chats::ID ? static_cast<const td::td_api::chats *>(r.get())->chat_ids_ : std::vector<std::int64_t>(); });
    });
}

void SearchModel::importPhoneContacts()
{
    const auto requestId = beginRequest(1);

    auto client = m_storage->client();
    auto contacts = phoneBookContacts();

    if (contacts.empty() || !client)
    {
        m_pending = 0;
        setLoading(false);

        // An address book with no numbers in it has been searched, and found nothing.
        m_searched = true;
        emit searchedChanged();

        return;
    }

    setLoading(true);

    client->send(td::td_api::make_object<td::td_api::importContacts>(std::move(contacts)), [this, requestId, alive = m_alive](auto &&response) {
        if (!alive->load())
            return;

        QVariantList userIds;

        if (response->get_id() == td::td_api::importedContacts::ID)
        {
            const auto *imported = static_cast<const td::td_api::importedContacts *>(response.get());

            for (auto userId : imported->user_ids_)
            {
                // Zero is TDLib's "this number has no Telegram account", which is most of
                // an address book.
                if (userId != 0)
                    userIds.append(QString::number(userId));
            }
        }
        else if (response->get_id() == td::td_api::error::ID)
        {
            const auto *error = static_cast<const td::td_api::error *>(response.get());
            qWarning() << "importContacts failed:" << error->code_ << QString::fromStdString(error->message_);
        }

        QMetaObject::invokeMethod(this, "handleResults", Qt::QueuedConnection, Q_ARG(int, requestId), Q_ARG(QVariantList, userIds));
    });
}

void SearchModel::handleResults(int requestId, const QVariantList &chatIds)
{
    if (requestId != m_requestId)
        return;  // a slower answer to a query that has since been typed past

    if (m_pending > 0)
        --m_pending;

    std::vector<Row> incoming;
    incoming.reserve(chatIds.size());

    for (const auto &rawChatId : chatIds)
    {
        const auto chatId = toId(rawChatId.toString());

        // The two halves of a search overlap: a contact with a public username is in both
        // answers.
        if (m_seenIds.contains(chatId))
            continue;

        Row row;
        row.id = chatId;
        row.chat = m_storage->chat(chatId);

        // A contact who has never been written to has no chat yet - the row is built from
        // the user, and the chat is created when it is tapped. See ChatManager::fetchChat.
        if (!row.chat)
        {
            row.user = m_storage->user(chatId);

            if (!row.user)
                continue;  // an id we were told about but hold nothing for
        }

        m_seenIds.insert(chatId);
        incoming.push_back(std::move(row));
    }

    if (!incoming.empty())
    {
        const auto first = static_cast<int>(m_rows.size());

        beginInsertRows(QModelIndex(), first, first + static_cast<int>(incoming.size()) - 1);

        for (auto &row : incoming)
        {
            ensurePhotoDownloaded(row);
            m_rows.push_back(std::move(row));
        }

        endInsertRows();

        emit countChanged();
    }

    // The other half of a search is still to come: staying on the spinner keeps "nothing
    // matched" off the screen until it is actually true.
    if (m_pending > 0)
        return;

    setLoading(false);

    if (!m_searched)
    {
        m_searched = true;
        emit searchedChanged();
    }
}

void SearchModel::ensurePhotoDownloaded(const Row &row) const
{
    const auto *photo = row.chat ? row.chat->photo() : row.user->photo();

    // Most rows here are people no conversation ever brought into the cache, so unlike the
    // chat list this cannot assume the avatar is already on disk. The delegate binds to the
    // File itself, which emits fileChanged when the download lands, so the row redraws
    // without the model hearing about it.
    //
    // ponytail: a row keeps the Chat or User it was built from, and updateUser replaces
    // that object rather than mutating it. An avatar still arrives - StorageManager's file
    // map hands the replacement the same File - but somebody who had no photo at all when
    // the row was built stays a placeholder. Rebuild the row on userUpdated if that shows
    // up in practice; these lists live for one search.
    if (!photo || photo->isDownloadingCompleted() || photo->isDownloadingActive() || !photo->canBeDownloaded())
        return;

    if (auto client = m_storage->client())
    {
        client->send(td::td_api::make_object<td::td_api::downloadFile>(photo->id(), 1, 0, 0, false));
    }
}
