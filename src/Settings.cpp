#include "Settings.hpp"

#include "Common.hpp"

#include <QCoreApplication>
#include <QSettings>

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()))
{
    m_invertedTheme = m_settings->value("invertedTheme", false).toBool();

    m_languagePackId = m_settings->value("languagePackId", DefaultLanguageCode).toString();
    m_languagePluralId = m_settings->value("languagePluralId", DefaultLanguageCode).toString();

    // Defaults false, so a first launch shows the sign-in screen with no round trip - which
    // is the correct screen for it, and the one case where guessing wrong costs nothing.
    m_wasAuthorized = m_settings->value("wasAuthorized", false).toBool();
}

bool Settings::invertedTheme() const
{
    return m_invertedTheme;
}

void Settings::setInvertedTheme(bool value)
{
    if (m_invertedTheme != value)
    {
        m_invertedTheme = value;
        m_settings->setValue("invertedTheme", m_invertedTheme);
        emit invertedThemeChanged();
    }
}

bool Settings::wasAuthorized() const
{
    return m_wasAuthorized;
}

void Settings::setWasAuthorized(bool value)
{
    if (m_wasAuthorized != value)
    {
        m_wasAuthorized = value;
        m_settings->setValue("wasAuthorized", m_wasAuthorized);
    }
}

QString Settings::languagePackId() const
{
    return m_languagePackId;
}

void Settings::setLanguagePackId(const QString &value)
{
    if (m_languagePackId != value)
    {
        m_languagePackId = value;
        m_settings->setValue("languagePackId", m_languagePackId);
        emit languagePackIdChanged();
    }
}

QString Settings::languagePluralId() const
{
    return m_languagePluralId;
}

void Settings::setLanguagePluralId(const QString &value)
{
    if (m_languagePluralId != value)
    {
        m_languagePluralId = value;
        m_settings->setValue("languagePluralId", m_languagePluralId);
        emit languagePluralIdChanged();
    }
}
