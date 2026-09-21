#pragma once

#include <QObject>
#include <QString>

#include <td/telegram/td_api.h>

#include <memory>

class StorageManager;

// The signed-in user's own profile, as the account settings page needs it: the fields that
// live on `user` are read straight back out of StorageManager, and the two that live on
// `userFullInfo` are fetched and kept here.
//
// Why not on StorageManager, which already keeps the bio for the profile page: the birthdate
// is read by exactly one screen, and widening the entity cache for it would mean every
// consumer of a user carrying a field only this page has ever wanted. Asking again is what
// TDLib is for, and userFullInfoUpdated says when to.
//
// Every setter is fire-and-forget: TDLib answers with `ok` and then pushes updateUser or
// updateUserFullInfo, which is what moves the properties. Nothing here writes its own copy
// optimistically, so a rejected change - a username already taken, a name the server will
// not accept - leaves the field showing what the account really says, and `failed` carries
// the reason to whatever wants to show it.
class Account : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString firstName READ firstName NOTIFY changed)
    Q_PROPERTY(QString lastName READ lastName NOTIFY changed)

    // Without the leading "@", which is a decoration for display rather than part of the
    // name. Empty when the account has none - which is most accounts.
    Q_PROPERTY(QString username READ username NOTIFY changed)

    // As TDLib gives it: digits with no "+" and no grouping. See ChatManager's
    // ponytail note about formatting one properly.
    Q_PROPERTY(QString phoneNumber READ phoneNumber NOTIFY changed)

    Q_PROPERTY(QString bio READ bio NOTIFY fullInfoChanged)

    // The birthdate as its parts, because the picker needs them separately, plus one
    // preformatted line for the row that displays it. Day is 0 when no birthdate is set,
    // and year is 0 when one is set without a year - which Telegram allows.
    Q_PROPERTY(int birthdateDay READ birthdateDay NOTIFY fullInfoChanged)
    Q_PROPERTY(int birthdateMonth READ birthdateMonth NOTIFY fullInfoChanged)
    Q_PROPERTY(int birthdateYear READ birthdateYear NOTIFY fullInfoChanged)
    Q_PROPERTY(QString birthdateText READ birthdateText NOTIFY fullInfoChanged)

public:
    explicit Account(std::shared_ptr<StorageManager> storage, QObject *parent = nullptr);

    QString firstName() const noexcept;
    QString lastName() const noexcept;
    QString username() const noexcept;
    QString phoneNumber() const noexcept;

    QString bio() const noexcept;

    int birthdateDay() const noexcept;
    int birthdateMonth() const noexcept;
    int birthdateYear() const noexcept;
    QString birthdateText() const noexcept;

public slots:
    // Pulls the full-info half. The page calls this when it opens: the fields on `user`
    // are always current, these two are only as current as the last time anybody asked.
    void load() noexcept;

    // TDLib takes both names in one request, so the page sends both whichever one changed.
    void setName(const QString &firstName, const QString &lastName) noexcept;

    void setBio(const QString &bio) noexcept;

    // An empty string removes the username, which is what TDLib does with one.
    void setUsername(const QString &username) noexcept;

    // year may be 0 for "no year", which Telegram allows and its own clients offer.
    void setBirthdate(int day, int month, int year) noexcept;

    // Removes it altogether - setBirthdate with a null birthdate, which is TDLib's own
    // way of saying "unset".
    void clearBirthdate() noexcept;

signals:
    void changed();
    void fullInfoChanged();

    // A request came back an error. Carries TDLib's message, because the useful ones are
    // specific - USERNAME_OCCUPIED, USERNAME_INVALID - and a generic "could not save"
    // would throw away the only thing that tells the user what to do next.
    void failed(const QString &message);

private slots:
    void handleUserUpdated(qlonglong userId);
    void handleFullInfoUpdated(qlonglong userId);

    // Queued from the getUserFullInfo callback, which runs on the TDLib worker thread.
    void setFullInfo(const QString &bio, int day, int month, int year) noexcept;

    // Same, for the reply to any of the setters.
    void reportFailure(const QString &message) noexcept;

private:
    // Sends `request` and routes an error reply to failed(). The whole difference between
    // the setters is which object they build.
    void send(td::td_api::object_ptr<td::td_api::Function> request) noexcept;

    std::shared_ptr<StorageManager> m_storage;

    QString m_bio;

    int m_birthdateDay{0};
    int m_birthdateMonth{0};
    int m_birthdateYear{0};
};
