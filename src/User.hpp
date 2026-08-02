#pragma once

#include "File.hpp"

#include <td/telegram/td_api.h>

#include <QDateTime>
#include <QObject>
#include <QStringList>

#include <memory>

class User : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qlonglong id READ id CONSTANT)
    Q_PROPERTY(QString firstName READ firstName CONSTANT)
    Q_PROPERTY(QString lastName READ lastName CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool isSupport READ isSupport CONSTANT)
    Q_PROPERTY(Type type READ type CONSTANT)

    // The person's own photo, which is theirs rather than any one conversation's - a
    // contact you have never written to has no chat to carry it. Same shape as
    // Chat::photo, because td_api::profilePhoto and td_api::chatPhotoInfo are the same
    // pair of files.
    Q_PROPERTY(File *photo READ photo NOTIFY photoChanged)

    Q_ENUMS(Type)
public:
    explicit User(td::td_api::object_ptr<td::td_api::user> user, QObject *parent = nullptr);

    enum Status { Empty, Online, Offline, Recently, LastWeek, LastMonth };
    enum Type { Bot, Regular, Deleted, Unknown };

    qlonglong id() const;
    QString firstName() const;
    QString lastName() const;
    Status status() const;
    bool isSupport() const;
    Type type() const;

    QDateTime wasOnline() const;
    QStringList activeUsernames() const;

    // Digits only, no leading "+". Empty when the other side's privacy settings hide it,
    // which is what makes showing it a matter of checking for a string.
    QString phoneNumber() const;

    void setStatus(td::td_api::object_ptr<td::td_api::UserStatus> status);

    File *photo() const noexcept;
    File *bigPhoto() const noexcept;

    // For StorageManager, so the user's photo and its file map hold one object - the same
    // arrangement Chat has, and what makes updateFile reach an avatar on screen.
    const std::shared_ptr<File> &photoFile() const noexcept;
    void adoptPhotoFile(std::shared_ptr<File> file) noexcept;
    const std::shared_ptr<File> &bigPhotoFile() const noexcept;
    void adoptBigPhotoFile(std::shared_ptr<File> file) noexcept;

signals:
    void statusChanged();
    void photoChanged();

private:
    Type determineType(const td::td_api::object_ptr<td::td_api::UserType> &type) const;

    void setProfilePhoto(td::td_api::object_ptr<td::td_api::profilePhoto> photo) noexcept;

    qlonglong m_id;
    QString m_firstName;
    QString m_lastName;
    Status m_status;
    bool m_isSupport;
    Type m_type;

    QDateTime m_wasOnline;
    QStringList m_activeUsernames;
    QString m_phoneNumber;

    // Shared with StorageManager's file map rather than owned outright, so updateFile
    // mutates the object this exposes. See registerUserPhoto().
    std::shared_ptr<File> m_file;
    std::shared_ptr<File> m_bigFile;
};
