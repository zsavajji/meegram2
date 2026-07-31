#include "File.hpp"

#include "ScopeTimer.hpp"

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
    // calls = updateFile packets reaching a bound File; the fileChanged scope below is
    // how many of them a delegate could see. Their ratio is what this guard bought.
    MEEGRAM_SCOPE("File::setFile");

    m_file = std::move(file);

    // TDLib sends an updateFile for every chunk that lands. Only five fields of it are
    // exposed here and an intermediate progress tick moves none of them - but
    // fileChanged notifies all five, so every binding reading a File would otherwise
    // re-run on every packet, for every visible row that happened to be downloading.
    // Emit only when a delegate could actually see a difference.
    //
    // Measured (docs/profiling.md): this suppresses 46% of notifications while photos
    // download, and 0% while avatars do. Avatars are small enough that every packet
    // they produce is a state transition - starts, completes - so there are no
    // intermediate ticks to filter. Which means the chat list, the case this was
    // written for, is the one place the guard cannot help; the win is on media.
    if (updateFileProperties())
    {
        MEEGRAM_SCOPE("File::fileChanged");
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
