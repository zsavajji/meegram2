#include "Account.hpp"

#include "Client.hpp"
#include "StorageManager.hpp"
#include "User.hpp"
#include "Utils.hpp"

#include <QDate>
#include <QDebug>

Account::Account(std::shared_ptr<StorageManager> storage, QObject *parent)
    : QObject(parent)
    , m_storage(std::move(storage))
{
    connect(m_storage.get(), SIGNAL(userUpdated(qlonglong)), SLOT(handleUserUpdated(qlonglong)));
    connect(m_storage.get(), SIGNAL(userFullInfoUpdated(qlonglong)), SLOT(handleFullInfoUpdated(qlonglong)));
}

QString Account::firstName() const noexcept
{
    const auto user = m_storage->user(m_storage->myId());

    return user ? user->firstName() : QString();
}

QString Account::lastName() const noexcept
{
    const auto user = m_storage->user(m_storage->myId());

    return user ? user->lastName() : QString();
}

QString Account::username() const noexcept
{
    const auto user = m_storage->user(m_storage->myId());

    if (!user)
        return QString();

    // The first is the primary one; the rest are aliases onto the same account.
    const auto usernames = user->activeUsernames();

    return usernames.isEmpty() ? QString() : usernames.first();
}

QString Account::phoneNumber() const noexcept
{
    const auto user = m_storage->user(m_storage->myId());

    return user ? user->phoneNumber() : QString();
}

QString Account::bio() const noexcept
{
    return m_bio;
}

int Account::birthdateDay() const noexcept
{
    return m_birthdateDay;
}

int Account::birthdateMonth() const noexcept
{
    return m_birthdateMonth;
}

int Account::birthdateYear() const noexcept
{
    return m_birthdateYear;
}

QString Account::birthdateText() const noexcept
{
    if (m_birthdateDay == 0 || m_birthdateMonth == 0)
        return QString();

    // QDate's own formatting, so the month name arrives in the device's locale rather than
    // from the language pack - which has no key for "12 August". A year of 0 is a birthday
    // without one, which Telegram allows, so the year is only in the string when it is real.
    const QDate date(m_birthdateYear == 0 ? 2000 : m_birthdateYear, m_birthdateMonth, m_birthdateDay);

    if (!date.isValid())
        return QString();

    return m_birthdateYear == 0 ? date.toString(QLatin1String("d MMMM")) : date.toString(QLatin1String("d MMMM yyyy"));
}

void Account::load() noexcept
{
    const auto userId = m_storage->myId();

    if (userId == 0)
        return;  // no my_id yet, so there is nothing to ask about

    auto client = m_storage->client();
    if (!client)
        return;

    client->send(td::td_api::make_object<td::td_api::getUserFullInfo>(userId), [this](auto &&response) {
        // TDLib worker thread: read the response and hand the values on, nothing else.
        if (response->get_id() != td::td_api::userFullInfo::ID)
            return;

        const auto *fullInfo = static_cast<const td::td_api::userFullInfo *>(response.get());

        const auto bio = Utils::formattedText(fullInfo->bio_);

        // birthdate_ is null for an account that has not set one, which is the common case.
        const auto *birthdate = fullInfo->birthdate_.get();

        QMetaObject::invokeMethod(this, "setFullInfo", Qt::QueuedConnection, Q_ARG(QString, bio),
                                  Q_ARG(int, birthdate ? birthdate->day_ : 0), Q_ARG(int, birthdate ? birthdate->month_ : 0),
                                  Q_ARG(int, birthdate ? birthdate->year_ : 0));
    });
}

void Account::setName(const QString &firstName, const QString &lastName) noexcept
{
    send(td::td_api::make_object<td::td_api::setName>(firstName.toStdString(), lastName.toStdString()));
}

void Account::setBio(const QString &bio) noexcept
{
    send(td::td_api::make_object<td::td_api::setBio>(bio.toStdString()));
}

void Account::setUsername(const QString &username) noexcept
{
    send(td::td_api::make_object<td::td_api::setUsername>(username.toStdString()));
}

void Account::setBirthdate(int day, int month, int year) noexcept
{
    send(td::td_api::make_object<td::td_api::setBirthdate>(td::td_api::make_object<td::td_api::birthdate>(day, month, year)));
}

void Account::clearBirthdate() noexcept
{
    send(td::td_api::make_object<td::td_api::setBirthdate>(nullptr));
}

void Account::send(td::td_api::object_ptr<td::td_api::Function> request) noexcept
{
    auto client = m_storage->client();
    if (!client)
        return;

    client->send(std::move(request), [this](auto &&response) {
        // Worker thread. Success is silence: TDLib follows an accepted change with
        // updateUser or updateUserFullInfo, and those are what move the properties.
        if (response->get_id() != td::td_api::error::ID)
            return;

        const auto *error = static_cast<const td::td_api::error *>(response.get());

        QMetaObject::invokeMethod(this, "reportFailure", Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(error->message_)));
    });
}

void Account::handleUserUpdated(qlonglong userId)
{
    if (userId == m_storage->myId())
        emit changed();
}

void Account::handleFullInfoUpdated(qlonglong userId)
{
    // Only that something changed, not what: StorageManager keeps the bio out of this and
    // nothing keeps the birthdate, so the answer is to ask again.
    if (userId == m_storage->myId())
        load();
}

void Account::setFullInfo(const QString &bio, int day, int month, int year) noexcept
{
    if (m_bio == bio && m_birthdateDay == day && m_birthdateMonth == month && m_birthdateYear == year)
        return;  // the reply to our own load(), with nothing new in it

    m_bio = bio;
    m_birthdateDay = day;
    m_birthdateMonth = month;
    m_birthdateYear = year;

    emit fullInfoChanged();
}

void Account::reportFailure(const QString &message) noexcept
{
    qWarning() << "account update rejected:" << message;

    emit failed(message);
}
