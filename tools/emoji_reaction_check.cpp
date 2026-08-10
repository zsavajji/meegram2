// The reaction emoji this client offers, checked against the emoji table without a phone.
//
// Telegram's reaction strings are not always spelled the way the emoji table keys them -
// the heart reaction is a bare U+2764 while the asset it draws is 2764-fe0f.png - and the
// mismatch is silent: Utils::emojiFilename hands back nothing, and the pill draws the
// character in whatever the system font has instead of the emoji. This walks
// Emoji::quickReactions() through the same three candidates that function tries and
// insists every one of them lands on an asset that is actually in the tree.
//
// The candidate rule is restated here rather than called: Utils.cpp cannot be linked on
// its own - it pulls in Chat, StorageManager, Locale and td_api - and this check exists to
// be runnable, not to be a second copy of the app. Keep the two in step; the rule is four
// lines and both name each other.
//
// Not wired into the build, and deliberately buildable with the host compiler alone:
//
//   g++ -std=c++20 -I src -I <qt4>/include -I <qt4>/include/QtCore \
//       tools/emoji_reaction_check.cpp src/Emoji.cpp -L <qt4>/lib -lQtCore \
//       -o /tmp/emoji_reaction_check && /tmp/emoji_reaction_check .

#include "Emoji.hpp"

#include <QFile>
#include <QHash>
#include <QString>

#include <cassert>
#include <cstdio>

namespace {

QString toQString(std::u16string_view value)
{
    return QString::fromUtf16(reinterpret_cast<const ushort *>(value.data()), value.size());
}

// Emoji::emojis() keyed by unicode, the same map Utils.cpp builds for replaceEmoji.
const QHash<QString, QString> &table()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> result;

        for (const Emoji &emoji : Emoji::emojis())
            result.insert(emoji.unicode(), emoji.filename());

        return result;
    }();

    return map;
}

// Mirrors Utils::emojiFilename. See the note at the top before changing either.
QString emojiFilename(const QString &emoji)
{
    static const QChar VariationSelector(0xFE0F);

    const QString candidates[] = {emoji, emoji + VariationSelector,
                                  emoji.endsWith(VariationSelector) ? emoji.left(emoji.size() - 1) : QString()};

    for (const auto &candidate : candidates)
    {
        if (candidate.isEmpty())
            continue;

        if (const auto it = table().constFind(candidate); it != table().constEnd())
            return it.value();
    }

    return QString();
}

}  // namespace

int main(int argc, char *argv[])
{
    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString(".");

    // The two spellings that made the normalisation necessary, pinned so a table
    // regenerated from a newer emoji list cannot quietly move them.
    assert(emojiFilename(QString::fromUtf8("\xF0\x9F\x91\x8D")) == QLatin1String("1f44d.png"));  // thumbs up, bare
    assert(emojiFilename(QString::fromUtf8("\xE2\x9D\xA4")) == QLatin1String("2764-fe0f.png"));  // heart, no selector
    assert(emojiFilename(QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F")) == QLatin1String("2764-fe0f.png"));  // heart, with one

    // Something that is not an emoji at all resolves to nothing rather than to a
    // neighbour - the pill falls back to drawing the character, which is the point.
    assert(emojiFilename(QLatin1String("x")).isEmpty());

    int failures = 0;

    for (const auto &reaction : Emoji::quickReactions())
    {
        const auto emoji = toQString(reaction);
        const auto filename = emojiFilename(emoji);

        if (filename.isEmpty())
        {
            std::printf("no table entry for reaction %s\n", qPrintable(emoji));
            ++failures;
            continue;
        }

        // The table naming a file is not the same as this build shipping it: emojiCategory
        // already skips entries whose asset is missing, so the picker can offer one too.
        const auto path = root + QLatin1String("/resources/emoji/") + filename;

        if (!QFile::exists(path))
        {
            std::printf("reaction %s resolves to %s, which is not in the tree\n", qPrintable(emoji), qPrintable(filename));
            ++failures;
            continue;
        }

        std::printf("ok %s -> %s\n", qPrintable(emoji), qPrintable(filename));
    }

    if (failures > 0)
    {
        std::printf("%d reaction(s) would draw nothing\n", failures);
        return 1;
    }

    std::printf("all %d reactions resolve\n", static_cast<int>(Emoji::quickReactions().size()));

    return 0;
}
