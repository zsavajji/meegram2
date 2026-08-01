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

public:
    explicit File(td::td_api::object_ptr<td::td_api::file> file, QObject *parent = nullptr);

    int id() const;

    QString localPath() const;

    QString size() const;

    bool canBeDownloaded() const;
    bool isDownloadingActive() const;
    bool isDownloadingCompleted() const;

    void setFile(td::td_api::object_ptr<td::td_api::file> file);

signals:
    void fileChanged();

private:
    // True when one of the exposed properties actually moved, which is what decides
    // whether fileChanged goes out.
    bool updateFileProperties();

    // Initialised, because a file that arrives with no local part leaves these
    // untouched and setFile now reads them back to compare.
    int m_id{};

    QString m_localPath;

    QString m_size;

    bool m_canBeDownloaded{};
    bool m_isDownloadingActive{};
    bool m_isDownloadingCompleted{};

    td::td_api::object_ptr<td::td_api::file> m_file;
};

Q_DECLARE_METATYPE(File *);
