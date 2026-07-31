#pragma once

#include "Common.hpp"
#include "PluralRules.hpp"

#include <td/telegram/td_api.h>

#include <QHash>
#include <QTranslator>

#include <memory>
#include <unordered_map>

class Locale : public QTranslator
{
    Q_OBJECT
public:
    explicit Locale(QObject *parent = nullptr);

    QString translate(const char *key, int plural = -1) const;
    QString translate(const char *context, const char *sourceText, const char *disambiguation) const override;

    QString getString(const char *key) const;
    QString formatPluralString(const char *key, int plural) const;
    QString formatCallDuration(int duration) const;
    QString formatTtl(int ttl, bool shorter = false) const;

    void setLanguagePlural(const QString &value);
    void setLanguagePackStrings(const QString &languageId, td::td_api::object_ptr<td::td_api::languagePackStrings> value);

    // The pack the previous run resolved, read straight off disk. Returns false if there
    // is nothing usable - no file, another language, or a format this build does not read.
    //
    // Exists because of *when*, not because of how often: TDLib persists the pack too, so
    // asking it is a local read rather than a download. But the answer comes back through
    // an async round trip, and on the daemon transport that is a ~1 MB reply landing after
    // the root QML page has already evaluated every qsTr on it. QML1 never retranslates,
    // so for those strings "later" means "never". This is the same data somewhere it can
    // be had before the scene is built.
    bool loadCache(const QString &languageId);

private:
    void saveCache(const QString &languageId) const;

    QString stringForQuantity(PluralRules::Quantity quantity) const;

    void addRules(const QStringList &languages, std::unique_ptr<PluralRules> rules);
    void updatePluralRules();

    QString m_languagePlural;
    QHash<QString, QString> m_languagePack;

    // getString() backs every tr() in the process and its output is a pure function
    // of the key plus the loaded language pack. Memoised here because the transform
    // costs two std::regex executions and ~8 allocations per call. Cleared whenever
    // the language pack is replaced.
    mutable QHash<QString, QString> m_stringCache;

    std::unique_ptr<PluralRules> m_currentPluralRules;
    std::unordered_map<QString, std::unique_ptr<PluralRules>> m_allRules;
};
