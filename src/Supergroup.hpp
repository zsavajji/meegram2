#pragma once

#include <td/telegram/td_api.h>

#include <QObject>
#include <QStringList>

class Supergroup : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qlonglong id READ id CONSTANT)
    Q_PROPERTY(Status status READ status CONSTANT)
    Q_PROPERTY(int memberCount READ memberCount CONSTANT)
    Q_PROPERTY(bool hasLocation READ hasLocation CONSTANT)
    Q_PROPERTY(bool isChannel READ isChannel CONSTANT)

    // A supergroup whose messages are filed under topics. Such a chat opens into its
    // topic list rather than straight into a message list - see ChatManager::isForum.
    Q_PROPERTY(bool isForum READ isForum CONSTANT)

public:
    explicit Supergroup(td::td_api::object_ptr<td::td_api::supergroup> group, QObject *parent = nullptr);

    enum Status { Creator, Administrator, Member, Restricted, Left, Banned };

    qlonglong id() const;
    Status status() const;
    int memberCount() const;
    bool hasLocation() const;
    bool isChannel() const;
    bool isForum() const;

    QStringList activeUsernames() const;

private:
    Status determineStatus(int statusId) const;

    qlonglong m_id;
    Status m_status;
    int m_memberCount;
    bool m_hasLocation;
    bool m_isChannel;
    bool m_isForum;

    QStringList m_activeUsernames;
};
