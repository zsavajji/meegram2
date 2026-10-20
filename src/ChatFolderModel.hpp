#pragma once

#include <td/telegram/td_api.h>

#include <QAbstractListModel>
#include <QStringList>

#include <memory>
#include <vector>

class ChatFolderInfo
{
    Q_GADGET

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)

public:
    explicit ChatFolderInfo(td::td_api::object_ptr<td::td_api::chatFolderInfo> info);

    int id() const;
    QString title() const;

private:
    int m_id;
    QString m_title;
};

class ChatFolderModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

    // The folder names in order, for MainPage's folder picker: a QStringList is the one list
    // type that crosses into QML (1 and 2) as a plain array. Notified with count, which
    // setItems emits on every update of the folder list.
    Q_PROPERTY(QStringList titles READ titles NOTIFY countChanged)
public:
    ChatFolderModel(QObject *parent = nullptr);

    enum ChatFolderRole { IdRole = Qt::UserRole + 1, TitleRole };

    void setItems(std::vector<std::shared_ptr<ChatFolderInfo>> chatFolders);

    int rowCount(const QModelIndex &index = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    Q_INVOKABLE QVariant get(int index) const noexcept;

    int count() const noexcept;
    QStringList titles() const;

signals:
    void countChanged();

private:
    std::vector<std::shared_ptr<ChatFolderInfo>> m_chatFolders;
};

Q_DECLARE_METATYPE(ChatFolderInfo *);
