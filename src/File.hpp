#pragma once

#include <td/telegram/td_api.h>

#include <QVariant>

#include <memory>

class File : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int id READ id NOTIFY fileChanged)

    Q_PROPERTY(QString localPath READ localPath NOTIFY fileChanged)

    // Already formatted ("1.4 MB"), not a byte count: td_api sizes are int53 and a
    // qlonglong crossing into QML1 comes out corrupted on this device. Nothing needs
    // the number anyway - it is only ever shown under a filename.
    Q_PROPERTY(QString size READ size NOTIFY fileChanged)

    Q_PROPERTY(bool canBeDownloaded READ canBeDownloaded NOTIFY fileChanged)
    Q_PROPERTY(bool isDownloadingActive READ isDownloadingActive NOTIFY fileChanged)
    Q_PROPERTY(bool isDownloadingCompleted READ isDownloadingCompleted NOTIFY fileChanged)

    // How much of the download has landed, 0 to 1. Its own signal and not fileChanged:
    // that one is emitted only when a field a delegate draws actually moved, which
    // suppresses 46% of updateFile packets while media downloads (docs/profiling.md) -
    // and progress moves on every single one of them. Notifying it through fileChanged
    // would hand that measurement straight back.
    //
    // double, not qreal: Qt 4's moc writes a qreal property's type as
    // QMetaType::QReal << 24, which on ARM is float (135) and lands in the sign bit.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

    // The part that has arrived, formatted the way size() is ("1.4 MB"), for the file row
    // that reads "1.4 MB / 6.2 MB" while a document downloads. Empty before the first
    // chunk lands.
    Q_PROPERTY(QString downloadedSize READ downloadedSize NOTIFY progressChanged)

public:
    explicit File(td::td_api::object_ptr<td::td_api::file> file, QObject *parent = nullptr);

    int id() const;

    QString localPath() const;

    QString size() const;

    bool canBeDownloaded() const;
    bool isDownloadingActive() const;
    bool isDownloadingCompleted() const;

    double progress() const;
    QString downloadedSize() const;

    void setFile(td::td_api::object_ptr<td::td_api::file> file);

signals:
    void fileChanged();
    void progressChanged();

private:
    // Which of the two groups of properties moved. Returned as a mask rather than a bool
    // so setFile can emit the two signals independently - the whole point of giving
    // progress its own, see above.
    enum Change { NoChange = 0, Fields = 1, Progress = 2 };

    int updateFileProperties(const td::td_api::file &file);

    // Initialised, because a file that arrives with no local part leaves these
    // untouched and setFile now reads them back to compare.
    int m_id{};

    QString m_localPath;

    QString m_size;

    bool m_canBeDownloaded{};
    bool m_isDownloadingActive{};
    bool m_isDownloadingCompleted{};

    double m_progress{};
    QString m_downloadedSize;

    // No td_api::file is kept: the five fields above are copied out, and the shell - two
    // remote id strings and the local path again - is freed with the update that carried
    // it. A synced account holds a few thousand of these.
};

Q_DECLARE_METATYPE(File *);
