#include "SessionModel.hpp"

#include "Client.hpp"
#include "Common.hpp"

#include <QDateTime>
#include <QDebug>
#include <QStringList>

namespace {

// One session as the delegate wants it: strings, already joined, nothing left to format in
// QML. Runs on the TDLib worker thread, so it touches nothing but its argument.
QVariantMap toRow(const td::td_api::object_ptr<td::td_api::session> &session) noexcept
{
    QVariantMap row;

    // Decimal string. A session id is an int64 and QML1 boxes anything past int32 as a
    // double, which comes back wrong - the rule that governs every id in this codebase.
    row["id"] = QString::number(session->id_);

    row["application"] = QString::fromStdString(session->application_name_) + QLatin1Char(' ') + QString::fromStdString(session->application_version_);

    row["device"] = QString::fromStdString(session->device_model_);

    // Platform and system version are each half a line: "MeeGo" and "1.2 Harmattan" mean
    // something together and very little apart.
    QStringList system;

    if (!session->platform_.empty())
        system << QString::fromStdString(session->platform_);

    if (!session->system_version_.empty())
        system << QString::fromStdString(session->system_version_);

    row["system"] = system.join(QLatin1String(" "));

    row["location"] = QString::fromStdString(session->location_);
    row["ipAddress"] = QString::fromStdString(session->ip_address_);

    // Seconds since the epoch, local time. The current session's last_active_date is now
    // by definition, so it is the login date that says anything there.
    const auto stamp = session->is_current_ ? session->log_in_date_ : session->last_active_date_;

    row["lastActive"] = stamp > 0 ? QDateTime::fromTime_t(static_cast<uint>(stamp)).toString(Qt::DefaultLocaleShortDate) : QString();

    row["isCurrent"] = session->is_current_;

    return row;
}

}  // namespace

SessionModel::SessionModel(std::shared_ptr<Client> client, QObject *parent)
    : QAbstractListModel(parent)
    , m_client(std::move(client))
{
    setRoleNames(roleNames());
}

SessionModel::~SessionModel()
{
    m_alive->store(false);
}

int SessionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_sessions.size();
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sessions.size())
        return QVariant();

    const auto row = m_sessions.at(index.row()).toMap();

    switch (role)
    {
        case IdRole:
            return row.value("id");
        case ApplicationRole:
            return row.value("application");
        case DeviceRole:
            return row.value("device");
        case SystemRole:
            return row.value("system");
        case LocationRole:
            return row.value("location");
        case IpAddressRole:
            return row.value("ipAddress");
        case LastActiveRole:
            return row.value("lastActive");
        case IsCurrentRole:
            return row.value("isCurrent");
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> SessionModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[IdRole] = "id";
    roles[ApplicationRole] = "application";
    roles[DeviceRole] = "device";
    roles[SystemRole] = "system";
    roles[LocationRole] = "location";
    roles[IpAddressRole] = "ipAddress";
    roles[LastActiveRole] = "lastActive";
    roles[IsCurrentRole] = "isCurrent";

    return roles;
}

int SessionModel::count() const noexcept
{
    return m_sessions.size();
}

bool SessionModel::loading() const noexcept
{
    return m_loading;
}

void SessionModel::setLoading(bool loading) noexcept
{
    if (m_loading == loading)
        return;

    m_loading = loading;

    emit loadingChanged();
}

void SessionModel::load() noexcept
{
    setLoading(true);

    m_client->send(td::td_api::make_object<td::td_api::getActiveSessions>(), [this, alive = m_alive](auto &&response) {
        if (!alive->load())
            return;

        if (response->get_id() != td::td_api::sessions::ID)
        {
            const auto message = response->get_id() == td::td_api::error::ID
                                     ? QString::fromStdString(static_cast<const td::td_api::error *>(response.get())->message_)
                                     : QString();

            QMetaObject::invokeMethod(this, "reportFailure", Qt::QueuedConnection, Q_ARG(QString, message));
            return;
        }

        const auto *sessions = static_cast<const td::td_api::sessions *>(response.get());

        QVariantList rows;

        for (const auto &session : sessions->sessions_)
        {
            if (!session)
                continue;

            // The current session first: it is the one the reader is standing in, and the
            // one row that is not an invitation to terminate anything.
            if (session->is_current_)
                rows.prepend(toRow(session));
            else
                rows.append(toRow(session));
        }

        QMetaObject::invokeMethod(this, "handleSessions", Qt::QueuedConnection, Q_ARG(QVariantList, rows));
    });
}

void SessionModel::handleSessions(const QVariantList &sessions)
{
    beginResetModel();
    m_sessions = sessions;
    endResetModel();

    setLoading(false);

    emit countChanged();
}

void SessionModel::terminate(const QString &sessionId) noexcept
{
    setLoading(true);

    m_client->send(td::td_api::make_object<td::td_api::terminateSession>(toId(sessionId)), [this, alive = m_alive](auto &&response) {
        if (!alive->load())
            return;

        if (response->get_id() == td::td_api::error::ID)
        {
            QMetaObject::invokeMethod(this, "reportFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(static_cast<const td::td_api::error *>(response.get())->message_)));
            return;
        }

        // Ask again rather than dropping the row here: the list on screen should be what
        // the server says it is, and this is one small request on a page nobody is
        // scrolling.
        QMetaObject::invokeMethod(this, "load", Qt::QueuedConnection);
    });
}

void SessionModel::terminateOthers() noexcept
{
    setLoading(true);

    m_client->send(td::td_api::make_object<td::td_api::terminateAllOtherSessions>(), [this, alive = m_alive](auto &&response) {
        if (!alive->load())
            return;

        if (response->get_id() == td::td_api::error::ID)
        {
            QMetaObject::invokeMethod(this, "reportFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(static_cast<const td::td_api::error *>(response.get())->message_)));
            return;
        }

        QMetaObject::invokeMethod(this, "load", Qt::QueuedConnection);
    });
}

void SessionModel::confirmQrLogin(const QString &link) noexcept
{
    setLoading(true);

    m_client->send(td::td_api::make_object<td::td_api::confirmQrCodeAuthentication>(link.toStdString()), [this, alive = m_alive](auto &&response) {
        if (!alive->load())
            return;

        if (response->get_id() == td::td_api::error::ID)
        {
            QMetaObject::invokeMethod(this, "reportFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(static_cast<const td::td_api::error *>(response.get())->message_)));
            return;
        }

        // The answer is the new session, but it is one row of a list this reloads anyway -
        // and reloading is what proves it actually took.
        QMetaObject::invokeMethod(this, "load", Qt::QueuedConnection);
    });
}

void SessionModel::reportFailure(const QString &message)
{
    setLoading(false);

    qWarning() << "sessions:" << message;

    emit failed(message);
}
