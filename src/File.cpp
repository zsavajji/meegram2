#include "File.hpp"

#include "ScopeTimer.hpp"
#include "Utils.hpp"

#include <cmath>


File::File(td::td_api::object_ptr<td::td_api::file> file, QObject *parent)
    : QObject(parent)
{
    if (file)
        updateFileProperties(*file);
}

int File::id() const
{
    return m_id;
}

QString File::localPath() const
{
    return m_localPath;
}

QString File::size() const
{
    return m_size;
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

double File::progress() const
{
    return m_progress;
}

QString File::downloadedSize() const
{
    return m_downloadedSize;
}

void File::setFile(td::td_api::object_ptr<td::td_api::file> file)
{
    // calls = updateFile packets reaching a bound File; the fileChanged scope below is
    // how many of them a delegate could see. Their ratio is what this guard bought.
    MEEGRAM_SCOPE("File::setFile");

    if (!file)
        return;

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
    const int changed = updateFileProperties(*file);

    if (changed & Fields)
    {
        MEEGRAM_SCOPE("File::fileChanged");
        emit fileChanged();
    }

    // Separate, so a row drawing a download ring repaints on a chunk while every other
    // binding on this file stays put.
    if (changed & Progress)
        emit progressChanged();
}

int File::updateFileProperties(const td::td_api::file &file)
{
    int changed = m_id != file.id_ ? Fields : NoChange;

    m_id = file.id_;

    // Total size, not downloaded size - it does not move as chunks land, so putting it
    // in the guard costs at most one extra notification (when an expected size is
    // replaced by the real one) rather than one per packet.
    const auto totalBytes = file.size_ > 0 ? file.size_ : file.expected_size_;

    const auto size = Utils::formatSize(totalBytes);

    if (m_size != size)
        changed |= Fields;

    m_size = size;

    // Same guard as before: a file with no local part leaves the download state
    // standing rather than clearing it.
    if (!file.local_)
        return changed;

    // Quantised to whole percent. TDLib sends an updateFile for every chunk that lands,
    // and a 32px ring cannot show a finer step than one percent anyway, so this bounds a
    // download to 100 notifications instead of one per packet.
    //
    // ponytail: one repaint per percent of any file, however large. Coarser steps if a
    // big download ever shows up in a scroll measurement.
    const double progress =
        totalBytes > 0 ? std::floor(100.0 * static_cast<double>(file.local_->downloaded_size_) / static_cast<double>(totalBytes)) / 100.0 : 0.0;

    const auto downloadedSize = file.local_->downloaded_size_ > 0 ? Utils::formatSize(file.local_->downloaded_size_) : QString();

    if (m_progress != progress || m_downloadedSize != downloadedSize)
        changed |= Progress;

    m_progress = progress;
    m_downloadedSize = downloadedSize;

    const auto localPath = QString::fromStdString(file.local_->path_);

    if (m_localPath != localPath || m_canBeDownloaded != file.local_->can_be_downloaded_ ||
        m_isDownloadingActive != file.local_->is_downloading_active_ || m_isDownloadingCompleted != file.local_->is_downloading_completed_)
        changed |= Fields;

    m_localPath = localPath;
    m_canBeDownloaded = file.local_->can_be_downloaded_;
    m_isDownloadingActive = file.local_->is_downloading_active_;
    m_isDownloadingCompleted = file.local_->is_downloading_completed_;

    return changed;
}
