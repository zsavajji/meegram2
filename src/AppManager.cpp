#include "AppManager.hpp"

#include "Authorization.hpp"
#include "Chat.hpp"
#include "ChatManager.hpp"
#include "Common.hpp"
#include "Localization.hpp"
#include "Settings.hpp"
#include "StorageManager.hpp"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QTimer>

#include <algorithm>

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

}  // namespace

AppManager::AppManager(QObject *parent)
    : QObject(parent)
    , m_client(std::make_shared<Client>())
    , m_authorization(std::make_shared<Authorization>(m_client))
    , m_locale(std::make_shared<Locale>())
    , m_settings(std::make_shared<Settings>())
    , m_storageManager(std::make_shared<StorageManager>(m_client))
{
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(close()));

    connect(m_client.get(), SIGNAL(result(td::td_api::Object *)), SLOT(handleResult(td::td_api::Object *)));

    // Here rather than in initialize(), and that is the whole point of it: initialize() is
    // called from main.qml's Component.onCompleted, by which time the root page has been
    // built and every qsTr on it evaluated. This constructor runs before setSource, so a
    // cached pack is in place for the first one. See Locale::loadCache.
    //
    // A hit also means startup has nothing to wait for - the request below still goes out
    // and still refreshes the pack, it just no longer holds the UI.
    if (m_locale->loadCache(m_settings->languagePackId()))
        m_initializationStatus[1] = true;

#ifdef MEEGRAM_JSON_TRANSPORT
    // Before authorization, and before the socket has said anything: a tap that started
    // this process is already on its way, and the D-Bus call that carries it is delivered
    // as soon as com.meegram is owned. Registering it later means dropping that first tap.
    m_notificationEndpoint = std::make_unique<NotificationEndpoint>();

    connect(m_notificationEndpoint.get(), SIGNAL(chatRequested(QString)), SIGNAL(chatRequested(QString)));
#endif
}

bool AppManager::isAuthorized() const noexcept
{
    return m_isAuthorized;
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

void AppManager::initialize() noexcept
{
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

    m_languagePackInfoModel = std::make_unique<LanguagePackInfoModel>(m_client);

    emit languagePackInfoModelChanged();
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
            // Queued, because this callback runs on the reader thread.
            QMetaObject::invokeMethod(this, "loadLanguagePack", Qt::QueuedConnection);
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
            // here and cannot work: under the daemon transport TDLib announced its
            // connection state long before this process attached, and a late-joining
            // client is never told again. That is the same reason requestAuthorizationState
            // exists, and there is no getConnectionState to do it with.
            //
            // Queued, because this callback runs on the reader thread and a QTimer belongs
            // to the thread that starts it.
            QMetaObject::invokeMethod(this, "scheduleLanguagePackRetry", Qt::QueuedConnection);
        }
    });
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

    emit chatManagerChanged();
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
