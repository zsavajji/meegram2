#include "AppManager.hpp"

#include "Account.hpp"
#include "Authorization.hpp"
#include "Chat.hpp"
#include "ChatManager.hpp"
#include "Common.hpp"
#include "Localization.hpp"
#include "SessionModel.hpp"
#include "Settings.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTimer>

#include <algorithm>
#include <optional>

namespace {

constexpr auto createLocale(const QString &languageCode) -> std::optional<QLocale>
{
    // clang-format off
    static const std::unordered_map<QString, QLocale> localeMap = {
        {"ab", QLocale(QLocale::Abkhazian, QLocale::AnyCountry)},
        {"om", QLocale(QLocale::Afan, QLocale::AnyCountry)},
        {"aa", QLocale(QLocale::Afar, QLocale::AnyCountry)},
        {"af", QLocale(QLocale::Afrikaans, QLocale::AnyCountry)},
        {"sq", QLocale(QLocale::Albanian, QLocale::AnyCountry)},
        {"am", QLocale(QLocale::Amharic, QLocale::AnyCountry)},
        {"ar", QLocale(QLocale::Arabic, QLocale::AnyCountry)},
        {"hy", QLocale(QLocale::Armenian, QLocale::AnyCountry)},
        {"as", QLocale(QLocale::Assamese, QLocale::AnyCountry)},
        {"ay", QLocale(QLocale::Aymara, QLocale::AnyCountry)},
        {"az", QLocale(QLocale::Azerbaijani, QLocale::AnyCountry)},
        {"ba", QLocale(QLocale::Bashkir, QLocale::AnyCountry)},
        {"eu", QLocale(QLocale::Basque, QLocale::AnyCountry)},
        {"bn", QLocale(QLocale::Bengali, QLocale::AnyCountry)},
        {"dz", QLocale(QLocale::Bhutani, QLocale::AnyCountry)},
        {"bh", QLocale(QLocale::Bihari, QLocale::AnyCountry)},
        {"bi", QLocale(QLocale::Bislama, QLocale::AnyCountry)},
        {"br", QLocale(QLocale::Breton, QLocale::AnyCountry)},
        {"bg", QLocale(QLocale::Bulgarian, QLocale::AnyCountry)},
        {"my", QLocale(QLocale::Burmese, QLocale::AnyCountry)},
        {"be", QLocale(QLocale::Byelorussian, QLocale::AnyCountry)},
        {"km", QLocale(QLocale::Cambodian, QLocale::AnyCountry)},
        {"ca", QLocale(QLocale::Catalan, QLocale::AnyCountry)},
        {"zh", QLocale(QLocale::Chinese, QLocale::AnyCountry)},
        {"co", QLocale(QLocale::Corsican, QLocale::AnyCountry)},
        {"hr", QLocale(QLocale::Croatian, QLocale::AnyCountry)},
        {"cs", QLocale(QLocale::Czech, QLocale::AnyCountry)},
        {"da", QLocale(QLocale::Danish, QLocale::AnyCountry)},
        {"nl", QLocale(QLocale::Dutch, QLocale::AnyCountry)},
        {"en", QLocale(QLocale::English, QLocale::AnyCountry)},
        {"eo", QLocale(QLocale::Esperanto, QLocale::AnyCountry)},
        {"et", QLocale(QLocale::Estonian, QLocale::AnyCountry)},
        {"fo", QLocale(QLocale::Faroese, QLocale::AnyCountry)},
        {"fj", QLocale(QLocale::FijiLanguage, QLocale::AnyCountry)},
        {"fi", QLocale(QLocale::Finnish, QLocale::AnyCountry)},
        {"fr", QLocale(QLocale::French, QLocale::AnyCountry)},
        {"fy", QLocale(QLocale::Frisian, QLocale::AnyCountry)},
        {"gd", QLocale(QLocale::Gaelic, QLocale::AnyCountry)},
        {"gl", QLocale(QLocale::Galician, QLocale::AnyCountry)},
        {"ka", QLocale(QLocale::Georgian, QLocale::AnyCountry)},
        {"de", QLocale(QLocale::German, QLocale::AnyCountry)},
        {"el", QLocale(QLocale::Greek, QLocale::AnyCountry)},
        {"kl", QLocale(QLocale::Greenlandic, QLocale::AnyCountry)},
        {"gn", QLocale(QLocale::Guarani, QLocale::AnyCountry)},
        {"gu", QLocale(QLocale::Gujarati, QLocale::AnyCountry)},
        {"ha", QLocale(QLocale::Hausa, QLocale::AnyCountry)},
        {"he", QLocale(QLocale::Hebrew, QLocale::AnyCountry)},
        {"hi", QLocale(QLocale::Hindi, QLocale::AnyCountry)},
        {"hu", QLocale(QLocale::Hungarian, QLocale::AnyCountry)},
        {"is", QLocale(QLocale::Icelandic, QLocale::AnyCountry)},
        {"id", QLocale(QLocale::Indonesian, QLocale::AnyCountry)},
        {"ia", QLocale(QLocale::Interlingua, QLocale::AnyCountry)},
        {"ie", QLocale(QLocale::Interlingue, QLocale::AnyCountry)},
        {"iu", QLocale(QLocale::Inuktitut, QLocale::AnyCountry)},
        {"ik", QLocale(QLocale::Inupiak, QLocale::AnyCountry)},
        {"ga", QLocale(QLocale::Irish, QLocale::AnyCountry)},
        {"it", QLocale(QLocale::Italian, QLocale::AnyCountry)},
        {"ja", QLocale(QLocale::Japanese, QLocale::AnyCountry)},
        {"jv", QLocale(QLocale::Javanese, QLocale::AnyCountry)},
        {"kn", QLocale(QLocale::Kannada, QLocale::AnyCountry)},
        {"ks", QLocale(QLocale::Kashmiri, QLocale::AnyCountry)},
        {"kk", QLocale(QLocale::Kazakh, QLocale::AnyCountry)},
        {"rw", QLocale(QLocale::Kinyarwanda, QLocale::AnyCountry)},
        {"ky", QLocale(QLocale::Kirghiz, QLocale::AnyCountry)},
        {"ko", QLocale(QLocale::Korean, QLocale::AnyCountry)},
        {"ku", QLocale(QLocale::Kurdish, QLocale::AnyCountry)},
        {"rn", QLocale(QLocale::Kurundi, QLocale::AnyCountry)},
        {"lo", QLocale(QLocale::Laothian, QLocale::AnyCountry)},
        {"la", QLocale(QLocale::Latin, QLocale::AnyCountry)},
        {"lv", QLocale(QLocale::Latvian, QLocale::AnyCountry)},
        {"ln", QLocale(QLocale::Lingala, QLocale::AnyCountry)},
        {"lt", QLocale(QLocale::Lithuanian, QLocale::AnyCountry)},
        {"mk", QLocale(QLocale::Macedonian, QLocale::AnyCountry)},
        {"mg", QLocale(QLocale::Malagasy, QLocale::AnyCountry)},
        {"ms-my", QLocale(QLocale::Malay, QLocale::Malaysia)},
        {"ms-sg", QLocale(QLocale::Malay, QLocale::Singapore)},
        {"ml", QLocale(QLocale::Malayalam, QLocale::AnyCountry)},
        {"mt", QLocale(QLocale::Maltese, QLocale::AnyCountry)},
        {"mi", QLocale(QLocale::Maori, QLocale::AnyCountry)},
        {"mr", QLocale(QLocale::Marathi, QLocale::AnyCountry)},
        {"mo", QLocale(QLocale::Moldavian, QLocale::AnyCountry)},
        {"mn", QLocale(QLocale::Mongolian, QLocale::AnyCountry)},
        {"na", QLocale(QLocale::NauruLanguage, QLocale::AnyCountry)},
        {"ne", QLocale(QLocale::Nepali, QLocale::AnyCountry)},
        {"no", QLocale(QLocale::Norwegian, QLocale::AnyCountry)},
        {"oc", QLocale(QLocale::Occitan, QLocale::AnyCountry)},
        {"or", QLocale(QLocale::Oriya, QLocale::AnyCountry)},
        {"ps", QLocale(QLocale::Pashto, QLocale::AnyCountry)},
        {"fa", QLocale(QLocale::Persian, QLocale::AnyCountry)},
        {"pl", QLocale(QLocale::Polish, QLocale::AnyCountry)},
        {"pt-br", QLocale(QLocale::Portuguese, QLocale::Brazil)},    // Brazilian Portuguese
        {"pt-pt", QLocale(QLocale::Portuguese, QLocale::Portugal)},  // European Portuguese
        {"pa", QLocale(QLocale::Punjabi, QLocale::AnyCountry)},
        {"qu", QLocale(QLocale::Quechua, QLocale::AnyCountry)},
        {"rm", QLocale(QLocale::RhaetoRomance, QLocale::AnyCountry)},
        {"ro", QLocale(QLocale::Romanian, QLocale::AnyCountry)},
        {"ru", QLocale(QLocale::Russian, QLocale::AnyCountry)},
        {"sm", QLocale(QLocale::Samoan, QLocale::AnyCountry)},
        {"sg", QLocale(QLocale::Sangho, QLocale::AnyCountry)},
        {"sa", QLocale(QLocale::Sanskrit, QLocale::AnyCountry)},
        {"sr", QLocale(QLocale::Serbian, QLocale::AnyCountry)},
        {"sh", QLocale(QLocale::SerboCroatian, QLocale::AnyCountry)},
        {"st", QLocale(QLocale::Sesotho, QLocale::AnyCountry)},
        {"tn", QLocale(QLocale::Setswana, QLocale::AnyCountry)},
        {"sn", QLocale(QLocale::Shona, QLocale::AnyCountry)},
        {"sd", QLocale(QLocale::Sindhi, QLocale::AnyCountry)},
        {"si", QLocale(QLocale::Singhalese, QLocale::AnyCountry)},
        {"ss", QLocale(QLocale::Siswati, QLocale::AnyCountry)},
        {"sk", QLocale(QLocale::Slovak, QLocale::AnyCountry)},
        {"sl", QLocale(QLocale::Slovenian, QLocale::AnyCountry)},
        {"so", QLocale(QLocale::Somali, QLocale::AnyCountry)},
        {"es", QLocale(QLocale::Spanish, QLocale::AnyCountry)},
        {"su", QLocale(QLocale::Sundanese, QLocale::AnyCountry)},
        {"sw", QLocale(QLocale::Swahili, QLocale::AnyCountry)},
        {"sv", QLocale(QLocale::Swedish, QLocale::AnyCountry)},
        {"tl", QLocale(QLocale::Tagalog, QLocale::AnyCountry)},
        {"tg", QLocale(QLocale::Tajik, QLocale::AnyCountry)},
        {"ta", QLocale(QLocale::Tamil, QLocale::AnyCountry)},
        {"tt", QLocale(QLocale::Tatar, QLocale::AnyCountry)},
        {"te", QLocale(QLocale::Telugu, QLocale::AnyCountry)},
        {"th", QLocale(QLocale::Thai, QLocale::AnyCountry)},
        {"bo", QLocale(QLocale::Tibetan, QLocale::AnyCountry)},
        {"ti", QLocale(QLocale::Tigrinya, QLocale::AnyCountry)},
        {"to", QLocale(QLocale::TongaLanguage, QLocale::AnyCountry)},
        {"tn", QLocale(QLocale::Tsonga, QLocale::AnyCountry)},
        {"tr", QLocale(QLocale::Turkish, QLocale::AnyCountry)},
        {"tk", QLocale(QLocale::Turkmen, QLocale::AnyCountry)},
        {"tw", QLocale(QLocale::Twi, QLocale::AnyCountry)},
        {"ug", QLocale(QLocale::Uigur, QLocale::AnyCountry)},
        {"uk", QLocale(QLocale::Ukrainian, QLocale::AnyCountry)},
        {"ur", QLocale(QLocale::Urdu, QLocale::AnyCountry)},
        {"uz", QLocale(QLocale::Uzbek, QLocale::AnyCountry)},
        {"vi", QLocale(QLocale::Vietnamese, QLocale::AnyCountry)},
        {"vo", QLocale(QLocale::Volapuk, QLocale::AnyCountry)},
        {"cy", QLocale(QLocale::Welsh, QLocale::AnyCountry)},
        {"wo", QLocale(QLocale::Wolof, QLocale::AnyCountry)},
        {"xh", QLocale(QLocale::Xhosa, QLocale::AnyCountry)},
        {"yi", QLocale(QLocale::Yiddish, QLocale::AnyCountry)},
        {"yo", QLocale(QLocale::Yoruba, QLocale::AnyCountry)},
        {"za", QLocale(QLocale::Zhuang, QLocale::AnyCountry)},
        {"zu", QLocale(QLocale::Zulu, QLocale::AnyCountry)},
        {"nn", QLocale(QLocale::NorwegianBokmal, QLocale::AnyCountry)},
        {"nb", QLocale(QLocale::NorwegianBokmal, QLocale::AnyCountry)},
        {"nyn", QLocale(QLocale::NorwegianNynorsk, QLocale::AnyCountry)},  // ### obsolete
        {"bs", QLocale(QLocale::Bosnian, QLocale::AnyCountry)},
        {"dv", QLocale(QLocale::Divehi, QLocale::AnyCountry)},
        {"gv", QLocale(QLocale::Manx, QLocale::AnyCountry)},
        {"kw", QLocale(QLocale::Cornish, QLocale::AnyCountry)},
        {"ak", QLocale(QLocale::Akan, QLocale::AnyCountry)},
        {"kn", QLocale(QLocale::Konkani, QLocale::AnyCountry)},
        {"ga", QLocale(QLocale::Ga, QLocale::AnyCountry)},
        {"ig", QLocale(QLocale::Igbo, QLocale::AnyCountry)},
        {"kam", QLocale(QLocale::Kamba, QLocale::AnyCountry)},
        {"sy", QLocale(QLocale::Syriac, QLocale::AnyCountry)},
        {"ti", QLocale(QLocale::Tigrinya, QLocale::AnyCountry)},
        {"bl", QLocale(QLocale::Blin, QLocale::AnyCountry)},
        {"gez", QLocale(QLocale::Geez, QLocale::AnyCountry)},
        {"kri", QLocale(QLocale::Koro, QLocale::AnyCountry)},
        {"sid", QLocale(QLocale::Sidamo, QLocale::AnyCountry)},
        {"aa", QLocale(QLocale::Atsam, QLocale::AnyCountry)},
        {"ti", QLocale(QLocale::Tigre, QLocale::AnyCountry)},
        {"ju", QLocale(QLocale::Jju, QLocale::AnyCountry)},
        {"fur", QLocale(QLocale::Friulian, QLocale::AnyCountry)},
        {"ve", QLocale(QLocale::Venda, QLocale::AnyCountry)},
        {"ee", QLocale(QLocale::Ewe, QLocale::AnyCountry)},
        {"wl", QLocale(QLocale::Walamo, QLocale::AnyCountry)},
        {"haw", QLocale(QLocale::Hawaiian, QLocale::AnyCountry)},
        {"ty", QLocale(QLocale::Tyap, QLocale::AnyCountry)},
        {"che", QLocale(QLocale::Chewa, QLocale::AnyCountry)},
        {"fil", QLocale(QLocale::Filipino, QLocale::AnyCountry)},
        {"gme", QLocale(QLocale::SwissGerman, QLocale::AnyCountry)},
        {"ii", QLocale(QLocale::SichuanYi, QLocale::AnyCountry)},
        {"kpe", QLocale(QLocale::Kpelle, QLocale::AnyCountry)},
        {"nds", QLocale(QLocale::LowGerman, QLocale::AnyCountry)},
        {"nso", QLocale(QLocale::SouthNdebele, QLocale::AnyCountry)},
        {"nse", QLocale(QLocale::NorthernSotho, QLocale::AnyCountry)},
        {"se", QLocale(QLocale::NorthernSami, QLocale::AnyCountry)},
        {"dav", QLocale(QLocale::Taita, QLocale::AnyCountry)},
        {"cgg", QLocale(QLocale::Chiga, QLocale::AnyCountry)},
        {"tzm", QLocale(QLocale::CentralMoroccoTamazight, QLocale::AnyCountry)},
        {"mas", QLocale(QLocale::Masai, QLocale::AnyCountry)},
        {"seh", QLocale(QLocale::Sena, QLocale::AnyCountry)}
    };

    // clang-format on

    if (auto it = localeMap.find(languageCode); it != localeMap.end())
    {
        return it->second;
    }
    return std::nullopt;  // Return empty optional if not found
}

// Long enough that a slow first sync is not accused of being stuck, short enough to be in
// the log before anyone reaches for a debugger.
constexpr int InitializationStallMs = 8000;

// The language pack is only ever missing because the network was, so this retries at the
// pace a network comes back at, not at the pace a request fails.
constexpr int LanguagePackRetryMs = 5000;

// How stale a cached language pack is allowed to get before it is pulled again.
//
// The pack answers with 1.8 MB on one line, measured on device, and Client::handleLine
// decodes a line before it can look at the next - so refreshing it on every launch spent
// seconds of the reader thread, and the socket behind it, on strings that change a few
// times a year. The disk cache has already answered every qsTr by the time this is
// decided; the request only ever picks up server-side edits.
//
// Two days: often enough that a translation fix lands within a couple of launches, rare
// enough that it is not something a launch pays for.
constexpr qint64 LanguagePackMaxAgeSeconds = 2 * 24 * 60 * 60;

// And when it is due, still not during the launch. Nothing waits on it, so it goes out
// once the things that are waited on have had the socket.
constexpr int LanguagePackRefreshMs = 10000;

// The window a notification tap gets to put its getChat on the socket ahead of the state
// replay. See scheduleStateRestore.
constexpr int StateRestoreDelayMs = 250;

// How long to wait before reopening a connection meegramd dropped, and how many times to
// try. Two seconds because the common causes all end with the daemon coming straight back
// - D-Bus reactivates it on the very connect below - and the first attempt is the one that
// has to lose a race with its listen(); five because a daemon that is not back by ten
// seconds is not restarting, it is gone.
constexpr int DaemonReconnectMs = 2000;
constexpr int DaemonReconnectAttempts = 5;

// How long one run of those attempts covers before a further disconnect counts as a new
// problem rather than the same one. A minute: long enough that a dropped-and-retried
// connection cannot loop, short enough that an app left open all day still recovers from
// an unrelated daemon restart hours later.
constexpr qint64 DaemonReconnectCooldownMs = 60000;

}  // namespace

AppManager::AppManager(QObject *parent)
    : QObject(parent)
    , m_client(std::make_shared<Client>())
    , m_authorization(std::make_shared<Authorization>(m_client))
    , m_locale(std::make_shared<Locale>())
    , m_settings(std::make_shared<Settings>())
    , m_storageManager(std::make_shared<StorageManager>(m_client))
    , m_account(std::make_shared<Account>(m_storageManager))
    , m_sessionModel(std::make_unique<SessionModel>(m_client))
{
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(close()));

    connect(m_client.get(), SIGNAL(result(td::td_api::Object *)), SLOT(handleResult(td::td_api::Object *)));

    // The socket is opened once, in Client's constructor, and every send after it dies is
    // dropped before it is encoded - so losing meegramd mid-run used to end the run, in
    // silence. Only the daemon transport can ever emit this; connected unconditionally so
    // the wiring does not have to agree with the build about which signals exist.
    connect(m_client.get(), SIGNAL(disconnected()), SLOT(handleDaemonGone()));

    // Here rather than in initialize(), and that is the whole point of it: initialize() is
    // called from main.qml's Component.onCompleted, by which time the root page has been
    // built and every qsTr on it evaluated. This constructor runs before setSource, so a
    // cached pack is in place for the first one. See Locale::loadCache.
    //
    // A hit also means startup has nothing to wait for - the request below still goes out
    // and still refreshes the pack, it just no longer holds the UI.
    if (m_locale->loadCache(m_settings->languagePackId()))
    {
        m_initializationStatus[1] = true;

        // Recorded so setParameters can hold the refresh back. See LanguagePackRefreshMs.
        m_localeFromCache = true;
    }

    // The last run's outcome, standing in until TDLib reports a real one. Without it the
    // only available answer this early is "not authorized", which is also what a signed-out
    // user looks like - and MainPage cannot tell those apart, so it showed the sign-in
    // screen to everyone during the gap.
    m_signedOut = !m_settings->wasAuthorized();

#ifdef MEEGRAM_JSON_TRANSPORT
    // Before authorization, and before the socket has said anything: a tap that started
    // this process is already on its way, and the D-Bus call that carries it is delivered
    // as soon as com.meegram is owned. Registering it later means dropping that first tap.
    m_notificationEndpoint = std::make_unique<NotificationEndpoint>();

    // Through handleChatRequested rather than straight to the signal. The comment above is
    // the reason this class owns the endpoint this early, and it is also the reason a
    // direct signal-to-signal connect loses the tap: nothing is listening yet.
    connect(m_notificationEndpoint.get(), SIGNAL(chatRequested(QString)), SLOT(handleChatRequested(QString)));
#endif
}

bool AppManager::isAuthorized() const noexcept
{
    return m_isAuthorized;
}

bool AppManager::isServiceUnreachable() const noexcept
{
    return m_serviceUnreachable;
}

const QString &AppManager::serviceError() const noexcept
{
    return m_serviceError;
}

bool AppManager::isSignedOut() const noexcept
{
    return m_signedOut;
}

const QString &AppManager::connectionStateString() const noexcept
{
    return m_connectionStateString;
}

Client *AppManager::client() const noexcept
{
    return m_client.get();
}

Authorization *AppManager::authorization() const noexcept
{
    return m_authorization.get();
}

Account *AppManager::account() const noexcept
{
    return m_account.get();
}

SessionModel *AppManager::sessionModel() const noexcept
{
    return m_sessionModel.get();
}

Locale *AppManager::locale() const noexcept
{
    return m_locale.get();
}

Settings *AppManager::settings() const noexcept
{
    return m_settings.get();
}

StorageManager *AppManager::storageManager() const noexcept
{
    return m_storageManager.get();
}

ChatManager *AppManager::chatManager() const noexcept
{
    return m_chatManager.get();
}

LanguagePackInfoModel *AppManager::languagePackInfoModel() const noexcept
{
    return m_languagePackInfoModel.get();
}

const QString &AppManager::cacheSize() const noexcept
{
    return m_cacheSize;
}

bool AppManager::privateChatsMuted() const noexcept
{
    return m_scopeMuted[0];
}

bool AppManager::groupChatsMuted() const noexcept
{
    return m_scopeMuted[1];
}

bool AppManager::channelChatsMuted() const noexcept
{
    return m_scopeMuted[2];
}

namespace {

// The three scopes in the order the settings page lists them. Null for an index that is
// not one of them, which the callers treat as "do nothing".
td::td_api::object_ptr<td::td_api::NotificationSettingsScope> toScope(int scope) noexcept
{
    switch (scope)
    {
        case 0:
            return td::td_api::make_object<td::td_api::notificationSettingsScopePrivateChats>();
        case 1:
            return td::td_api::make_object<td::td_api::notificationSettingsScopeGroupChats>();
        case 2:
            return td::td_api::make_object<td::td_api::notificationSettingsScopeChannelChats>();
        default:
            return nullptr;
    }
}

// What a switch means by "muted". Telegram's own clients write this rather than a year in
// seconds, and anything above the current time reads as muted to every other client.
constexpr auto MuteForever = 2147483647;

}  // namespace

void AppManager::requestStorageStatistics() noexcept
{
    m_client->send(td::td_api::make_object<td::td_api::getStorageStatisticsFast>(), [this](auto &&response) {
        // TDLib worker thread: read it and hand the number on.
        if (response->get_id() != td::td_api::storageStatisticsFast::ID)
            return;

        const auto *statistics = static_cast<const td::td_api::storageStatisticsFast *>(response.get());

        // files_size only. The databases are the chat list and the history, which clearCache
        // deliberately does not touch, so counting them here would offer to free space that
        // clearing cannot free.
        QMetaObject::invokeMethod(this, "setCacheBytes", Qt::QueuedConnection, Q_ARG(qlonglong, statistics->files_size_));
    });
}

void AppManager::setCacheBytes(qlonglong bytes) noexcept
{
    // TDLib's files plus this app's own avatar cache, which is not in its accounting and is
    // 16 KiB per avatar per size - a few hundred chats' worth of it.
    QDir avatars(QDir::homePath() + QLatin1String("/.meegram/avatars"));

    if (avatars.exists())
    {
        const auto cached = avatars.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

        for (const auto &entry : cached)
        {
            bytes += entry.size();
        }
    }

    const auto formatted = Utils::formatSize(bytes);

    if (m_cacheSize == formatted)
        return;

    m_cacheSize = formatted;

    emit cacheSizeChanged();
}

void AppManager::clearCache() noexcept
{
    // Every zero is "no limit of this kind": no size ceiling to stay under, no age cutoff,
    // no count cutoff, and no immunity window protecting recent files. Empty vectors are
    // every file type and every chat. The result is "delete what can be re-downloaded".
    auto request = td::td_api::make_object<td::td_api::optimizeStorage>(0, 0, 0, 0, std::vector<td::td_api::object_ptr<td::td_api::FileType>>(),
                                                                       std::vector<std::int64_t>(), std::vector<std::int64_t>(), false, 0);

    m_client->send(std::move(request), [this](auto &&) {
        // Whatever it says, ask again rather than guessing: the answer is a storageStatistics
        // whose shape this does not otherwise need, and the number on screen has to come from
        // the same place it came from before.
        QMetaObject::invokeMethod(this, "requestStorageStatistics", Qt::QueuedConnection);
    });

    // Ours to clear, and TDLib will not: same list AppManager::logOut removes.
    QDir avatars(QDir::homePath() + QLatin1String("/.meegram/avatars"));

    if (avatars.exists())
    {
        const auto cached = avatars.entryList(QDir::Files | QDir::NoDotAndDotDot);

        for (const auto &name : cached)
        {
            avatars.remove(name);
        }
    }
}

void AppManager::loadNotificationSettings() noexcept
{
    for (int scope = 0; scope < 3; ++scope)
    {
        auto request = td::td_api::make_object<td::td_api::getScopeNotificationSettings>(toScope(scope));

        m_client->send(std::move(request), [this, scope](auto &&response) {
            if (response->get_id() != td::td_api::scopeNotificationSettings::ID)
                return;

            const auto *settings = static_cast<const td::td_api::scopeNotificationSettings *>(response.get());

            QMetaObject::invokeMethod(this, "setScopeMuteState", Qt::QueuedConnection, Q_ARG(int, scope),
                                      Q_ARG(bool, settings->mute_for_ > 0));
        });
    }
}

void AppManager::setScopeMuteState(int scope, bool muted) noexcept
{
    if (scope < 0 || scope > 2 || m_scopeMuted[scope] == muted)
        return;

    m_scopeMuted[scope] = muted;

    emit notificationSettingsChanged();
}

void AppManager::setScopeMuted(int scope, bool muted) noexcept
{
    auto target = toScope(scope);

    if (!target)
        return;

    // Read, change one field, write it back. A blind setScopeNotificationSettings would
    // reset show_preview, the notification sound and the pinned-message and mention
    // switches to whatever this app happened to send - choices this app has no UI for and
    // therefore no business overwriting.
    auto request = td::td_api::make_object<td::td_api::getScopeNotificationSettings>(toScope(scope));

    m_client->send(std::move(request), [this, scope, muted](auto &&response) {
        if (response->get_id() != td::td_api::scopeNotificationSettings::ID)
            return;

        auto settings = td::td_api::move_object_as<td::td_api::scopeNotificationSettings>(response);

        settings->mute_for_ = muted ? MuteForever : 0;

        // Still the worker thread, and Client::send is what both threads use to reach
        // TDLib - so the write goes out from here rather than costing a hop each way.
        m_client->send(td::td_api::make_object<td::td_api::setScopeNotificationSettings>(toScope(scope), std::move(settings)), {});

        QMetaObject::invokeMethod(this, "setScopeMuteState", Qt::QueuedConnection, Q_ARG(int, scope), Q_ARG(bool, muted));
    });
}

void AppManager::clearSession() noexcept
{
    if (m_chatManager)
        m_chatManager->reset();

    m_storageManager->clear();
}

void AppManager::logOut() noexcept
{
    // Before the request, not after: logOut takes the network with it and the reply that
    // confirms it is the authorization state going to Closed, by which point the daemon
    // this is talking to may already be on its way out. Nothing here needs the network.
    //
    // QDir::removeRecursively is Qt 5. This directory is one flat level of files that
    // ChatPhotoProvider wrote itself, so a listing and an unlink each is the whole job -
    // and if a name in there is not ours to remove, leaving it is better than recursing
    // through a home directory.
    QDir avatars(QDir::homePath() + QLatin1String("/.meegram/avatars"));

    if (avatars.exists())
    {
        const auto cached = avatars.entryList(QDir::Files | QDir::NoDotAndDotDot);

        for (const auto &name : cached)
        {
            if (!avatars.remove(name))
                qWarning() << "logOut: could not remove cached avatar" << name;
        }
    }

    m_authorization->logOut();
}

void AppManager::close() noexcept
{
#ifndef MEEGRAM_JSON_TRANSPORT
    m_client->send(td::td_api::make_object<td::td_api::close>(), {});
#endif
    // With the daemon transport this instance does not own TDLib - meegramd does, and it
    // is shared. Sending close on aboutToQuit closed the daemon's client too, which is
    // exactly the connection the daemon exists to keep alive: the process kept running,
    // its TDLib did not, and nothing arrived until it was restarted. meegramd closes its
    // own client when the session bus goes away.
}

void AppManager::setOption(const QString &name, const QVariant &value)
{
    td::td_api::object_ptr<td::td_api::OptionValue> optionValue;

    switch (value.type())
    {
        case QVariant::Bool:
            optionValue = td::td_api::make_object<td::td_api::optionValueBoolean>(value.toBool());
            break;
        case QVariant::Int:
        case QVariant::LongLong:
            optionValue = td::td_api::make_object<td::td_api::optionValueInteger>(value.toLongLong());
            break;
        case QVariant::String:
            optionValue = td::td_api::make_object<td::td_api::optionValueString>(value.toString().toStdString());
            break;
        default:
            optionValue = td::td_api::make_object<td::td_api::optionValueEmpty>();
    }

    m_client->send(td::td_api::make_object<td::td_api::setOption>(name.toStdString(), std::move(optionValue)));
}

void AppManager::downloadFile(int fileId, int priority, qlonglong offset, qlonglong limit, bool synchronous)
{
    m_client->send(td::td_api::make_object<td::td_api::downloadFile>(fileId, priority, offset, limit, synchronous));
}

void AppManager::handleChatRequested(const QString &chatId) noexcept
{
    if (!m_qmlReady)
    {
        m_pendingChatId = chatId;
        return;
    }

    emit chatRequested(chatId);
}

void AppManager::initialize() noexcept
{
    // Before anything below can return early. main.qml calls this from
    // Component.onCompleted, so from here a chatRequested has somewhere to land.
    m_qmlReady = true;

    if (!m_pendingChatId.isEmpty())
    {
        // Back through the same slot, which now takes the other branch and emits. Queued,
        // not direct: this runs inside Component.onCompleted, and the handler it wakes
        // calls pageStack.pop(null, true) on a stack whose initial page is still being
        // completed. Deferring it to the next trip through the event loop lets the scene
        // finish first. main.qml holds it again from there if TDLib has not authorized
        // yet, which on this path it has not.
        QMetaObject::invokeMethod(this, "handleChatRequested", Qt::QueuedConnection, Q_ARG(QString, m_pendingChatId));

        m_pendingChatId.clear();
    }

    m_client->send(td::td_api::make_object<td::td_api::getOption>("version"), {});

    setOption("language_pack_database_path", QString(QDir::homePath() + DatabaseDirectory + "/langpack"));
    setOption("localization_target", "android");
    setOption("language_pack_id", m_settings->languagePackId());

    setParameters();
    requestAuthorizationState();

    // No loadLanguagePack here: it goes out from the setParameters callback, once TDLib
    // has something to answer it with.

    // Says why the spinner is still there, once, and stops waiting for the half that can
    // never arrive on its own.
    QTimer::singleShot(InitializationStallMs, this, SLOT(reportInitializationStall()));

    // Built once. retry() runs this whole function again, and make_unique here would
    // destroy the previous model synchronously - while the settings page, if it is open,
    // is still bound to it. Nothing about it is tied to the connection: it holds a list of
    // languages and asks for it on demand, so the existing one works against the new
    // socket unchanged.
    if (!m_languagePackInfoModel)
    {
        m_languagePackInfoModel = std::make_unique<LanguagePackInfoModel>(m_client);

        emit languagePackInfoModelChanged();
    }
}

bool AppManager::retry() noexcept
{
    // The reconnect is the half that cannot be skipped: with the socket gone, Client::send
    // drops every request before it is encoded, so re-running initialize() on its own
    // would send nothing at all and the screen would go back to a spinner that resolves no
    // better than the last one.
    if (!m_client->reconnect())
    {
        qWarning() << "retry: still nothing to connect to; leaving the message up";
        return false;
    }

    m_serviceUnreachable = false;
    emit serviceUnreachableChanged();

    // Back to the spinner, with a fresh stall deadline behind it - so an attempt that goes
    // the same way as the last one puts the message back up on its own, and the button
    // with it.
    //
    // ponytail: a reconnect while signed in leaves the chat list as stale as the moment the
    // socket died - handleAuthorizationState returns early once there is a ChatManager, so
    // the getCurrentState replay does not run a second time. Everything sent from here on
    // works and new updates arrive; what is missing is whatever happened while nothing was
    // listening. Replay into live models if that ever shows.
    initialize();

    return true;
}

void AppManager::handleDaemonGone() noexcept
{
    // The budget is per *episode*, not per disconnect. A connection that lived for an hour
    // and then died is a new problem and gets five fresh attempts; five disconnects inside
    // a minute are one problem being retried, and resetting on each of them is an
    // unbounded loop - which is precisely what a daemon that accepts a connection and
    // closes it immediately produces. That is not hypothetical: a peer check this binary
    // could not pass did it 5461 times in a single run.
    const auto now = QDateTime::currentMSecsSinceEpoch();

    if (now - m_lastReconnectEpisode >= DaemonReconnectCooldownMs)
    {
        m_lastReconnectEpisode = now;
        m_reconnectsLeft = DaemonReconnectAttempts;
    }

    if (m_reconnectsLeft <= 0)
    {
        qWarning() << "meegramd went away again within the cooldown; not reconnecting";
        return;
    }

    qWarning() << "meegramd went away; reconnecting in" << DaemonReconnectMs << "ms";

    QTimer::singleShot(DaemonReconnectMs, this, SLOT(reconnectToDaemon()));
}

void AppManager::reconnectToDaemon() noexcept
{
    if (retry())
        return;

    if (--m_reconnectsLeft <= 0)
    {
        // The end of the automatic half. The manual one is still there - MainPage's button
        // if startup never finished, and the next launch otherwise.
        qWarning() << "meegramd did not come back after" << DaemonReconnectAttempts << "attempts; giving up";
        return;
    }

    QTimer::singleShot(DaemonReconnectMs, this, SLOT(reconnectToDaemon()));
}

void AppManager::setParameters() noexcept
{
    auto request = td::td_api::make_object<td::td_api::setTdlibParameters>();

    request->database_directory_ = QString(QDir::homePath() + DatabaseDirectory).toStdString();
    request->use_file_database_ = true;
    request->use_chat_info_database_ = true;
    request->use_message_database_ = true;
    request->use_secret_chats_ = true;
    request->api_id_ = ApiId;
    request->api_hash_ = ApiHash;
    request->system_language_code_ = DefaultLanguageCode.toStdString();
    request->device_model_ = DeviceModel;
    request->system_version_ = SystemVersion;
    request->application_version_ = AppVersion;

    // request->use_test_dc_ = true;

    m_client->send(std::move(request), [this](auto &&response) {
        // A TDLib that is already running rejects a second setTdlibParameters, and that
        // is what a UI attaching to a live meegramd always sends. The rejection means the
        // parameters *are* set, which is the only thing this flag records, so it counts
        // as success - otherwise appInitialized() never fires and the app never starts.
        //
        // Matched on the message because TDLib gives it no distinct code: it is a plain
        // 400 (td/telegram/Requests.cpp, on_request for setTdlibParameters).
        const auto alreadyRunning = [&response] {
            if (response->get_id() != td::td_api::error::ID)
                return false;

            return static_cast<const td::td_api::error &>(*response).message_ == "Unexpected setTdlibParameters";
        };

        if (response->get_id() == td::td_api::ok::ID || alreadyRunning())
        {
            m_initializationStatus[0] = true;
            checkInitializationStatus();

            // Only now. TDLib answers getLanguagePackStrings with an error until it has
            // parameters, and this request used to go out one line after setTdlibParameters
            // - so on a first launch it lost the race with the database opening and failed
            // every time, leaving the retry below as the only way startup could finish.
            // Asking after the parameters are accepted makes the first attempt the one that
            // works.
            //
            // Unless a cached pack already answered it and is still fresh, in which case
            // this launch asks for nothing at all - see LanguagePackMaxAgeSeconds. A stale
            // one is refreshed, but later: nothing is waiting on it except the socket.
            //
            // Queued, because this callback runs on the reader thread.
            if (!m_localeFromCache)
                QMetaObject::invokeMethod(this, "loadLanguagePack", Qt::QueuedConnection);
            else if (languagePackIsStale())
                QMetaObject::invokeMethod(this, "refreshLanguagePack", Qt::QueuedConnection);
        }
    });
}

void AppManager::requestAuthorizationState() noexcept
{
    // The state may have last changed before this process existed - see the note on
    // Client::injectUpdate. Asking is the only way a late-attaching UI can find out.
    //
    // Sent straight after setParameters, which on a fresh client is still in flight;
    // TDLib queues this behind it and answers with a real state rather than the
    // placeholder it reports before initialization. On the in-process transport the
    // answer is simply the state TDLib was about to announce anyway, so replaying it
    // costs one redundant update and keeps the two transports behaving identically.
    m_client->send(td::td_api::make_object<td::td_api::getAuthorizationState>(), [this](auto &&response) {
        if (!response || response->get_id() == td::td_api::error::ID)
            return;

        auto state = td::td_api::move_object_as<td::td_api::AuthorizationState>(response);

        m_client->injectUpdate(td::td_api::make_object<td::td_api::updateAuthorizationState>(std::move(state)));
    });
}

// ponytail: a delay rather than a signal, because nothing in the app reports "the
// notification tap has arrived, or is never going to". The tap is a D-Bus call dispatched
// by the event loop and authorization is a queued update delivered by the same loop, and
// measured on device they land 17 ms apart in an order nothing guarantees - so the tap
// gets a window to ask first rather than a coin toss. A signal to close it on would need
// the tap to be latched in one place instead of two (here and main.qml), which is a
// restructure this does not need.
//
// The cost on a launch with no tap is 250 ms before a replay that takes seconds, and the
// chat list cannot render ahead of it either way.
void AppManager::scheduleStateRestore() noexcept
{
    QTimer::singleShot(StateRestoreDelayMs, this, SLOT(restoreState()));
}

// Everything TDLib announces exactly once, asked for rather than waited on.
//
// requestAuthorizationState above solves this for the login state; it is not special.
// updateNewChat is sent once per chat per TDLib process - send_update_new_chat latches
// d->is_update_new_chat_sent (td/telegram/MessagesManager.cpp) and nothing clears it - and
// so are updateUser, updateSupergroup, updateBasicGroup, updateChatFolders, updateOption
// and updateConnectionState. In process that is invisible, because a fresh client walks
// every one of them from the beginning on each launch. Against meegramd it is not: its
// TDLib outlives the UI by design, so a UI started against a running daemon is told none of
// it, and loadChats then answers 404 because the chat list genuinely is loaded. The chat
// list stayed empty for the rest of the run - docs/profiling.md carried that as an open
// defect - and behind it sat the same hole for user names, folder tabs, my_id and the
// connection state.
//
// getCurrentState is TDLib's own answer, and its description says so outright: "Returns all
// updates needed to restore current TDLib state ... especially useful if TDLib is run in a
// separate process" (td_api.tl). One request, and the reply is the updates themselves, so
// replaying them through injectUpdate means StorageManager and every model take their
// normal path and nothing else in the app has to know this happened.
//
// The whole state used to arrive as one line, and on a real account that line is 5.5 MB:
// 543 updateNewChat, 524 updateChatLastMessage, 343 updateUser and 930 KB of full-info
// siblings, measured on device. Client::handleLine decodes a line before it looks at the
// next, so it cost ~12 seconds of the reader thread and everything behind it waited -
// which is what made a tapped notification take twenty seconds to open its chat.
//
// meegramd now relays it as its constituent updates, one per line, and answers this
// request with a plain ok (see broadcastSplit in src/daemon/main.cpp). They arrive through
// the ordinary update path, in order - which is load-bearing: TDLib emits supergroups
// before the basic groups that name them, users before the secret chats that name them,
// and updateNewChat before the updateChatLastMessage that carries the chat's positions.
void AppManager::restoreState() noexcept
{
    m_client->send(td::td_api::make_object<td::td_api::getCurrentState>(), [this](auto &&response) {
        if (!response)
            return;

        // The replay has already been delivered, update by update. Nothing left to do but
        // release the handler, which returning does.
        if (response->get_id() == td::td_api::ok::ID)
            return;

        if (response->get_id() != td::td_api::updates::ID)
            return;

        // A daemon that predates the split answers with the whole thing. Worth keeping:
        // meegramd is the resident process, so an upgraded UI meets the old one every time
        // the app is reinstalled without a reboot, and the alternative failure is a chat
        // list that stays empty for the rest of the session.
        auto updates = td::td_api::move_object_as<td::td_api::updates>(response);

        for (auto &update : updates->updates_)
        {
            if (update)
                m_client->injectUpdate(std::move(update));
        }
    });
}

void AppManager::loadLanguagePack() noexcept
{
    const auto &languageCode = m_settings->languagePackId();

    auto request = td::td_api::make_object<td::td_api::getLanguagePackStrings>();
    request->language_pack_id_ = languageCode.toStdString();

    m_client->send(std::move(request), [this, languageCode](auto &&response) noexcept {
        if (response->get_id() == td::td_api::languagePackStrings::ID)
        {
            const auto &languagePlural = m_settings->languagePluralId();

            m_locale->setLanguagePlural(languagePlural);
            m_locale->setLanguagePackStrings(languageCode, td::td_api::move_object_as<td::td_api::languagePackStrings>(response));

            if (auto locale = createLocale(languageCode); locale)
            {
                QLocale::setDefault(*locale);
            }
            else
            {
                qDebug() << "Locale not found for language code:" << languageCode;
            }

            if (!m_initializationStatus[1])
            {
                m_initializationStatus[1] = true;
                checkInitializationStatus();
            }

            // Queued: this runs on the reader thread and QSettings is not shared safely
            // across threads. Recorded only on success, so a failed pull leaves the pack
            // due and the next launch tries again.
            QMetaObject::invokeMethod(this, "recordLanguagePackFetched", Qt::QueuedConnection);
        }
        else
        {
            // The response was an error, which offline on a first launch is what this
            // always is: there is nothing in the local pack database to answer from. The
            // callback used to stop here, so the second initialization flag was never set,
            // appInitialized() never fired, and the app sat on the spinner for the rest of
            // the run - including after the network came back, because nothing asked again.
            //
            // Retried whether or not startup has given up waiting: the deadline in
            // reportInitializationStall releases the UI, it does not stop the pack from
            // arriving, and every page pushed after it lands is translated normally.
            //
            // A timer rather than a connection-state hook, which was the first thing tried
            // here and could not work: under the daemon transport TDLib announced its
            // connection state long before this process attached, and there is no
            // getConnectionState to ask with. restoreState() has since closed that hole -
            // getCurrentState replays updateConnectionState along with everything else - so
            // a hook is now possible. The timer is kept because it also covers the case the
            // hook never did: the pack request failing while the connection is perfectly
            // fine.
            //
            // Queued, because this callback runs on the reader thread and a QTimer belongs
            // to the thread that starts it.
            QMetaObject::invokeMethod(this, "scheduleLanguagePackRetry", Qt::QueuedConnection);
        }
    });
}

bool AppManager::languagePackIsStale() const noexcept
{
    const auto fetchedAt = m_settings->languagePackFetchedAt();

    // Never fetched, or a clock that has moved backwards - a device whose time was wrong
    // and got corrected would otherwise sit on a pack it can never decide to refresh.
    const auto now = QDateTime::currentMSecsSinceEpoch() / 1000;
    if (fetchedAt <= 0 || fetchedAt > now)
        return true;

    return now - fetchedAt >= LanguagePackMaxAgeSeconds;
}

void AppManager::refreshLanguagePack() noexcept
{
    QTimer::singleShot(LanguagePackRefreshMs, this, SLOT(loadLanguagePack()));
}

void AppManager::recordLanguagePackFetched() noexcept
{
    m_settings->setLanguagePackFetchedAt(QDateTime::currentMSecsSinceEpoch() / 1000);
}

void AppManager::scheduleLanguagePackRetry() noexcept
{
    qWarning() << "language pack request failed; retrying in" << LanguagePackRetryMs << "ms";

    QTimer::singleShot(LanguagePackRetryMs, this, SLOT(loadLanguagePack()));
}

void AppManager::reportInitializationStall() noexcept
{
    if (std::all_of(m_initializationStatus.begin(), m_initializationStatus.end(), [](const auto &status) { return status.load(); }))
        return;

    // The one line this used to be missing. A spinner that never resolves said nothing
    // about which half of startup was stuck, and the two have entirely different causes:
    // no parameters means nothing is coming back over the socket at all, no language pack
    // means the socket works and the request failed.
    qWarning() << "startup stalled after" << InitializationStallMs << "ms - setTdlibParameters"
               << (m_initializationStatus[0] ? "ok" : "PENDING") << "| language pack" << (m_initializationStatus[1] ? "ok" : "PENDING")
               << "| connection" << (m_connectionStateString.isEmpty() ? QLatin1String("never reported") : QLatin1String("reported"))
               << m_connectionStateString << "| authorized" << m_isAuthorized;

    // The half that cannot release itself. Nothing has come back from TDLib at all - a
    // failed connect, a connection the daemon's peer check refused, or a daemon that is not
    // there - and the spinner has nothing to resolve to. The warning above reaches no log
    // the user can read either: D-Bus activation and the launcher both run the app through
    // invoker, which discards stderr. So say it on screen instead; MainPage swaps the
    // spinner for it.
    //
    // Deliberately no automatic retry from here, which is what this used to do and had to
    // be taken back out. retry() calls initialize(), initialize() arms this deadline, and a
    // transport that reconnects but still never answers comes straight back here - so the
    // screen and the spinner alternated on a ten-second cycle for as long as the app was
    // open, which is worse than the dead end it replaced. The button is the retry at
    // startup; the automatic one belongs to handleDaemonGone, which fires on a real
    // disconnect and cannot re-trigger itself.
    //
    // A cold D-Bus activation of meegramd claims the bus name in 0.10 s and has its socket
    // bound 0.02 s later, measured with 900 MB pushed through the page cache first - so
    // nothing here is waiting on a slow start, and there is nothing for a timer to win.
    if (!m_initializationStatus[0])
    {
        // The transport's own account of itself, if it has one. It does not when the socket
        // opened and TDLib simply never answered over it, which is a different failure and
        // has to read as one - the daemon is there, it is what is behind the daemon that is
        // not. Untranslated for the same reason the screen is: on a first launch this is
        // what has stopped the language pack from arriving.
        m_serviceError = m_client->lastConnectError();

        if (m_serviceError.isEmpty())
            m_serviceError = QLatin1String("meegramd answered, but TDLib behind it did not.");

        m_serviceUnreachable = true;
        emit serviceUnreachableChanged();
    }

    // And stop waiting for it. Startup waits for the language pack because QML1 never
    // retranslates (see MainPage.qml), but a first launch with no local pack and no
    // network cannot be a spinner with no way out - that state has no way to sign in,
    // which is the one thing a first launch is for. Untranslated is a worse app; no app
    // is not an app.
    //
    // Set unconditionally, not only when the parameters are in: if those are the half
    // that is late, this is still the last timer that runs, and gating it on them would
    // put the spinner right back where it was.
    if (!m_initializationStatus[1])
    {
        m_initializationStatus[1] = true;
        checkInitializationStatus();
    }
}

void AppManager::checkInitializationStatus() noexcept
{
    if (std::all_of(m_initializationStatus.begin(), m_initializationStatus.end(), [](const auto &status) { return status.load(); }))
    {
        emit appInitialized();
    }
}

void AppManager::handleResult(td::td_api::Object *object)
{
    if (object->get_id() == td::td_api::updateAuthorizationState::ID)
    {
        handleAuthorizationState(*static_cast<const td::td_api::updateAuthorizationState *>(object)->authorization_state_);
    }

    if (object->get_id() == td::td_api::updateConnectionState::ID)
    {
        handleConnectionState(*static_cast<const td::td_api::updateConnectionState *>(object)->state_);
    }
}

void AppManager::handleAuthorizationState(const td::td_api::AuthorizationState &authorizationState)
{
    // Recorded before the Ready test below, which returns early for everything else and so
    // never learned that a user is signed *out*. Only the states that need the user to do
    // something count: WaitTdlibParameters is startup plumbing every launch passes through,
    // and treating it as signed out would put the sign-in screen up on every cold start -
    // exactly the flash this exists to remove.
    const auto signedOut = [&authorizationState] {
        switch (authorizationState.get_id())
        {
            case td::td_api::authorizationStateWaitPhoneNumber::ID:
            case td::td_api::authorizationStateWaitPremiumPurchase::ID:
            case td::td_api::authorizationStateWaitEmailAddress::ID:
            case td::td_api::authorizationStateWaitEmailCode::ID:
            case td::td_api::authorizationStateWaitCode::ID:
            case td::td_api::authorizationStateWaitOtherDeviceConfirmation::ID:
            case td::td_api::authorizationStateWaitRegistration::ID:
            case td::td_api::authorizationStateWaitPassword::ID:
            case td::td_api::authorizationStateLoggingOut::ID:
            case td::td_api::authorizationStateClosed::ID:
                return std::optional<bool>(true);
            case td::td_api::authorizationStateReady::ID:
                return std::optional<bool>(false);
            default:
                // WaitTdlibParameters, Closing: says nothing either way, so leave the
                // seeded guess in place.
                return std::optional<bool>();
        }
    }();

    if (signedOut.has_value() && *signedOut != m_signedOut)
    {
        m_signedOut = *signedOut;

        // Persisted here rather than only on Ready, so a sign-out is remembered too and
        // the next launch opens straight onto the sign-in screen.
        m_settings->setWasAuthorized(!m_signedOut);

        emit signedOutChanged();

        // Signing out leaves a ChatManager and a StorageManager full of an account that is
        // gone, and handleAuthorizationState only ever *creates* a ChatManager - so without
        // this, signing back in without restarting shows the previous account's chats.
        //
        // Cleared rather than destroyed, and queued rather than immediate. See
        // clearSession().
        if (m_signedOut)
            QTimer::singleShot(0, this, SLOT(clearSession()));
    }

    if (authorizationState.get_id() != td::td_api::authorizationStateReady::ID)
        return;

    m_isAuthorized = true;
    emit authorizedChanged();

    if (m_chatManager)
        return;

    m_chatManager = std::make_shared<ChatManager>(m_storageManager, m_locale);

#ifndef MEEGRAM_JSON_TRANSPORT
    // Only the in-process transport builds one of these. Under the daemon transport
    // meegramd has been posting notifications since before this app was started, and it
    // learns which chat is on screen from the openChat it relays - so there is nothing to
    // construct and nothing to tell.
    m_notificationManager = std::make_unique<NotificationManager>(m_storageManager, m_locale);

    connect(m_chatManager.get(), SIGNAL(activeChatChanged(qlonglong)), m_notificationManager.get(), SLOT(setActiveChat(qlonglong)));
    connect(m_notificationManager.get(), SIGNAL(chatRequested(QString)), SIGNAL(chatRequested(QString)));
#endif

    // Before the state restore below, which is the whole ordering: main.qml releases a held
    // notification tap from here, and everything that tap needs - getChat, then the first
    // getChatHistory - has to be on the socket ahead of the replay. One socket, one reader
    // thread, decoded strictly in order, so whichever is asked for first the other waits.
    emit chatManagerChanged();

#ifdef MEEGRAM_JSON_TRANSPORT
    // After the ChatManager, so its models are connected to StorageManager before the
    // replay reaches it - and after the m_chatManager guard above, which is what stops the
    // updateAuthorizationState inside the replay from starting a second one.
    scheduleStateRestore();
#endif
}

void AppManager::handleConnectionState(const td::td_api::ConnectionState &connectionState)
{
    // All five ConnectionState variants. connectionStateConnectingToProxy used to be
    // missing even though TopBar.qml already handled the string, and an unmapped
    // state left m_connectionStateString at its previous value - so the header could
    // sit on a stale "Connecting" forever with nothing to indicate why.
    static const std::unordered_map<int, std::string> stateMap = {
        {td::td_api::connectionStateReady::ID, "Ready"},
        {td::td_api::connectionStateConnecting::ID, "Connecting"},
        {td::td_api::connectionStateConnectingToProxy::ID, "ConnectingToProxy"},
        {td::td_api::connectionStateUpdating::ID, "Updating"},
        {td::td_api::connectionStateWaitingForNetwork::ID, "WaitingForNetwork"}};

    const auto it = stateMap.find(connectionState.get_id());

    if (it == stateMap.end())
    {
        qWarning() << "Unmapped connection state id:" << connectionState.get_id();
        return;
    }

    const auto state = QString::fromStdString(it->second);

    if (state == m_connectionStateString)
        return;

    m_connectionStateString = state;
    emit connectionStateChanged();

}
