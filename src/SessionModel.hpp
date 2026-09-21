#pragma once

#include <td/telegram/td_api.h>

#include <QAbstractListModel>

#include <atomic>
#include <memory>

class Client;

// Every device and application this account is signed in on. Shaped like
// LanguagePackInfoModel - one settings page, one request, a plain vector behind it - rather
// than like ChatModel, because nothing here arrives as an update: TDLib has no
// updateActiveSessions, so the list is what getActiveSessions last said and `load()` is the
// only thing that moves it.
class SessionModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit SessionModel(std::shared_ptr<Client> client, QObject *parent = nullptr);
    ~SessionModel() override;

    enum SessionRoles {
        // A decimal string, like every id crossing into QML - a session id is an int64 and
        // QML1 would box it as a double. See Common.hpp.
        IdRole = Qt::UserRole + 1,
        // "MeeGram 0.4.0", the application rather than the hardware.
        ApplicationRole,
        // "Nokia N9", or whatever the other end reported.
        DeviceRole,
        // "MeeGo 1.2, Harmattan" - platform and system version joined, because neither is
        // worth a line of its own.
        SystemRole,
        LocationRole,
        IpAddressRole,
        // "last seen" as a date, or the login date for the current session, which is never
        // anything but now.
        LastActiveRole,
        // The session this app is running as. It cannot be terminated from here - TDLib
        // rejects it, and logging out is what that would mean anyway.
        IsCurrentRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const;

    int count() const noexcept;
    bool loading() const noexcept;

public slots:
    void load() noexcept;

    // Ends one session. The list is reloaded from the answer rather than the row being
    // dropped locally: a terminate that the server refused would otherwise leave a row
    // missing from a list that still has it.
    void terminate(const QString &sessionId) noexcept;

    // Signs another device in from the QR code it is showing. `link` is the tg://login?token
    // the scanner read; TDLib answers with the Session it created, and the list reloads so
    // the new device is on it.
    void confirmQrLogin(const QString &link) noexcept;

    // Everything except this device. Its own request rather than a loop, because TDLib has
    // one and a loop would be one round trip per session.
    void terminateOthers() noexcept;

signals:
    void countChanged();
    void loadingChanged();

    void failed(const QString &message);

private slots:
    // Queued from the callbacks, which run on the TDLib worker thread.
    void handleSessions(const QVariantList &sessions);
    void reportFailure(const QString &message);

private:
    void setLoading(bool loading) noexcept;

    std::shared_ptr<Client> m_client;

    bool m_loading{false};

    // Rows as QVariantMaps rather than td_api objects: everything here is a string by the
    // time it reaches a delegate, the list is short, and it means nothing of TDLib's
    // survives the worker thread.
    QVariantList m_sessions;

    // Liveness token for the callbacks, which Client invokes on the TDLib worker thread.
    // AppManager keeps this model for the life of the process, so today it can only be
    // false during shutdown - it is here so that moving the model onto the page that shows
    // it stays a one-line change rather than a use-after-free.
    std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
};
