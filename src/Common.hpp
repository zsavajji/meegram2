#pragma once

#include <QHash>
#include <QLocale>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

// Telegram ids cross the QML boundary as decimal strings, never as qlonglong.
//
// QtScript keeps a number that fits in int32 as an immediate integer, so it marshals
// through QVariant(int) and arrives intact - which is why private chats, whose id is a
// small positive user id, always worked. Anything larger is boxed as a double and
// converted with QVariant(double).toLongLong(), and on this build that conversion
// corrupts the low 16 bits: QML sent chat id -1001383801308 and openChat received
// -1001383814072, top 48 bits identical. Every supergroup id and every message id
// (message ids are id << 20) is in the affected range.
//
// A JS number passed to a const QString & parameter is converted by QScriptValue::
// toString(), which is exact, and toLongLong() parses it with integer arithmetic. No
// floating point anywhere, so QML call sites need no change.
inline qlonglong toId(const QString &value) noexcept
{
    return value.toLongLong();
}

constexpr auto AppName = "MeeGram";
constexpr auto AppVersion = "0.3.0";

constexpr auto ApiId = 142713;
constexpr auto ApiHash = "9e9e687a70150c6436afe3a2b6bfd7d7";

constexpr auto DatabaseDirectory = "/.meegram/tdlib";

static auto DefaultLanguageCode = QLocale::system().name().left(2);

constexpr auto DeviceModel = "Nokia N9";
constexpr auto SystemVersion = "MeeGo 1.2 Harmattan";

constexpr std::array<int, 3> ServiceNotificationsUserIds = {42777, 333000, 777000};

constexpr auto ChatSliceLimit = 25;
constexpr auto ContactSearchLimit = 20;
constexpr auto MaxImportedContacts = 5000u;
constexpr auto MessageSliceLimit = 20;

constexpr auto MutedValueMax = 2147483647;  // int32.max = 2^32 - 1
constexpr auto MutedValueMin = 0;

namespace std {
template <>
struct hash<QString>
{
    std::size_t operator()(const QString &s) const
    {
        return qHash(s);
    }
};

}  // namespace std
