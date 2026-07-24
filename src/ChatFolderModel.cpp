#include "ChatFolderModel.hpp"

namespace {

// chatFolderInfo used to carry a plain `title:string`. It now holds
// `name:chatFolderName`, which wraps a formattedText, so the folder title lives two
// levels down. Both pointers are checked because either can be null in principle.
QString folderTitle(const td::td_api::chatFolderInfo &info) noexcept
{
    if (!info.name_ || !info.name_->text_)
        return {};

    return QString::fromStdString(info.name_->text_->text_);
}

}  // namespace

ChatFolderInfo::ChatFolderInfo(td::td_api::object_ptr<td::td_api::chatFolderInfo> info)
    : m_id(info->id_)
    , m_title(folderTitle(*info))
{
}

int ChatFolderInfo::id() const
{
    return m_id;
}

QString ChatFolderInfo::title() const
{
    return m_title;
}

ChatFolderModel::ChatFolderModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QHash<int, QByteArray> roles;
    roles.insert(IdRole, "id");
    roles.insert(TitleRole, "name");

    setRoleNames(roles);
}

void ChatFolderModel::setItems(std::vector<std::shared_ptr<ChatFolderInfo>> chatFolders)
{
    beginResetModel();
    m_chatFolders = std::move(chatFolders);
    endResetModel();

    emit countChanged();
}

int ChatFolderModel::rowCount(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return static_cast<int>(m_chatFolders.size());
}

QVariant ChatFolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_chatFolders.size()))
        return QVariant();

    auto folderPtr = m_chatFolders.at(index.row());

    if (!folderPtr)
        return QVariant();

    switch (role)
    {
        case IdRole:
            return folderPtr->id();
        case TitleRole:
            return folderPtr->title();
        default:
            return QVariant();
    }
}

QVariant ChatFolderModel::get(int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(m_chatFolders.size()))
    {
        return QVariant();
    }

    return QVariant::fromValue(m_chatFolders.at(index).get());
}

int ChatFolderModel::count() const noexcept
{
    return static_cast<int>(m_chatFolders.size());
}
