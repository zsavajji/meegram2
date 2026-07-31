#pragma once

#include <td/telegram/td_api.h>

#include "MessageContent.hpp"

#include <QMetaType>
#include <QStringList>

class MessageService : public QObject, public MessageContent
{
    Q_OBJECT

    Q_PROPERTY(QString groupTitle READ groupTitle CONSTANT)
    Q_PROPERTY(QStringList addedMembers READ addedMembers CONSTANT)
    Q_PROPERTY(qlonglong removedMember READ removedMember CONSTANT)
    Q_PROPERTY(int autoDeleteTime READ autoDeleteTime CONSTANT)
    Q_PROPERTY(QString customAction READ customAction CONSTANT)

public:
    explicit MessageService(td::td_api::object_ptr<td::td_api::MessageContent> content, QObject *parent = nullptr);

    QString groupTitle() const;
    QStringList addedMembers() const;
    qlonglong removedMember() const;
    int autoDeleteTime() const;
    QString customAction() const;

private:
    QString m_groupTitle;
    QStringList m_addedMembers;
    qlonglong m_removedMember{0};
    int m_autoDeleteTime{0};
    QString m_customAction;
};

Q_DECLARE_METATYPE(MessageService *)
