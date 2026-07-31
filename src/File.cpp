#include "File.hpp"

File::File(td::td_api::object_ptr<td::td_api::file> file, QObject *parent)
    : QObject(parent)
    , m_file(std::move(file))
{
    updateFileProperties();
}

int File::id() const
{
    return m_id;
}

QString File::localPath() const
{
    return m_localPath;
}

bool File::canBeDownloaded() const
{
    return m_canBeDownloaded;
}

bool File::isDownloadingActive() const
{
    return m_isDownloadingActive;
}

bool File::isDownloadingCompleted() const
{
    return m_isDownloadingCompleted;
}

void File::setFile(td::td_api::object_ptr<td::td_api::file> file)
{
    m_file = std::move(file);

    // TDLib sends an updateFile for every chunk that lands. Only five fields of it are
    // exposed here and a progress tick moves none of them - but fileChanged notifies
    // all five, so every binding reading a File used to re-run on every packet, for
    // every visible row that happened to be downloading. That is the chat list's worst
    // moment: you flick, the avatars start arriving, and the rows you are dragging
    // re-evaluate their source binding on each one. Emit only when a delegate could
    // actually see a difference.
    if (updateFileProperties())
    {
        emit fileChanged();
    }
}

bool File::updateFileProperties()
{
    if (!m_file)
        return false;

    bool changed = m_id != m_file->id_;

    m_id = m_file->id_;

    // Same guard as before: a file with no local part leaves the download state
    // standing rather than clearing it.
    if (!m_file->local_)
        return changed;

    const auto localPath = QString::fromStdString(m_file->local_->path_);

    changed = changed || m_localPath != localPath || m_canBeDownloaded != m_file->local_->can_be_downloaded_ ||
              m_isDownloadingActive != m_file->local_->is_downloading_active_ ||
              m_isDownloadingCompleted != m_file->local_->is_downloading_completed_;

    m_localPath = localPath;
    m_canBeDownloaded = m_file->local_->can_be_downloaded_;
    m_isDownloadingActive = m_file->local_->is_downloading_active_;
    m_isDownloadingCompleted = m_file->local_->is_downloading_completed_;

    return changed;
}
