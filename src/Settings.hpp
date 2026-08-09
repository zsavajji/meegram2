#pragma once

#include <QObject>
#include <QStringList>

class QSettings;

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool invertedTheme READ invertedTheme WRITE setInvertedTheme NOTIFY invertedThemeChanged)

    Q_PROPERTY(QString languagePackId READ languagePackId WRITE setLanguagePackId NOTIFY languagePackIdChanged)
    Q_PROPERTY(QString languagePluralId READ languagePluralId WRITE setLanguagePluralId NOTIFY languagePluralIdChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    bool invertedTheme() const;
    void setInvertedTheme(bool value);

    QString languagePackId() const;
    void setLanguagePackId(const QString &value);

    QString languagePluralId() const;
    void setLanguagePluralId(const QString &value);

    // How the last run ended: signed in, or not. Read before TDLib has said anything, so
    // startup can show the right screen instead of the one that means "we have not asked
    // yet" - see AppManager::isSignedOut. Wrong only across a sign-out performed by
    // another client, and corrected the moment TDLib reports a real state.
    bool wasAuthorized() const;
    void setWasAuthorized(bool value);

    // When the whole language pack was last pulled from the server, as a Unix time in
    // seconds, or 0 if it never has been. The pack answers with 1.8 MB on one line and the
    // disk cache already has every string the UI asks for, so a launch that refreshed it
    // would be spending the socket - and the reader thread that decodes it - on strings
    // that change a few times a year. See AppManager::LanguagePackMaxAgeSeconds.
    qint64 languagePackFetchedAt() const;
    void setLanguagePackFetchedAt(qint64 value);

signals:
    void invertedThemeChanged();

    void languagePackIdChanged();
    void languagePluralIdChanged();

private:
    QSettings *m_settings{};

    bool m_invertedTheme;
    bool m_wasAuthorized;

    qint64 m_languagePackFetchedAt;

    QString m_languagePackId;
    QString m_languagePluralId;
};
