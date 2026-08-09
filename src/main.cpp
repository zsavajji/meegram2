#include <QApplication>
#include <QDeclarativeComponent>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeError>
#include <QDeclarativeView>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QModelIndex>
#include <QPainter>
#include <QPixmapCache>
#include <QStringList>
#include <QTextCodec>

#include <atomic>
#include <chrono>

#ifdef MEEGRAM_GL_VIEWPORT
#include <QGLWidget>
#endif

// malloc_trim, for returning freed arena pages to the kernel after the scene teardown.
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "AppManager.hpp"
#include "Authorization.hpp"
#include "Log.hpp"
#include "Chat.hpp"
#include "ChatManager.hpp"
#include "ChatPhotoProvider.hpp"
#include "ChatPosition.hpp"
#include "Client.hpp"
#include "Common.hpp"
#include "File.hpp"
#include "LanguagePackInfoModel.hpp"
#include "Localization.hpp"
#include "LottieAnimation.hpp"
#include "Message.hpp"
#include "MessageService.hpp"
#include "QrCodeItem.hpp"
#include "VoiceNote.hpp"
#include "ScopeTimer.hpp"
#include "Settings.hpp"
#include "StickerProvider.hpp"
#include "StorageManager.hpp"
#include "Utils.hpp"

namespace {

// Qt's default handler is this same fprintf, so installing one changes nothing about where
// the output goes - openLog has already pointed stderr at the file, which is what captures
// qWarning, TDLib and the GL driver alike. What it buys is somewhere to check the file's
// size that costs no timer: the only thing that grows the log is a message passing through
// here, and an idle phone must not be woken to look at a file nothing has written to.
void logMessage(QtMsgType type, const char *message)
{
    std::fprintf(stderr, "%s\n", message);

    // Every 256th line rather than every one: a binding warning firing per frame would
    // otherwise cost an fstat per frame. Atomic because qWarning is called from the TDLib
    // reader thread as well as this one.
    static std::atomic<int> sinceCheck{0};

    if (sinceCheck.fetch_add(1, std::memory_order_relaxed) >= 256)
    {
        sinceCheck = 0;
        rotateLogIfLarge();
    }

    // What the default handler does, and the reason qFatal is fatal.
    if (type == QtFatalMsg)
        std::abort();
}

#ifdef MEEGRAM_PROFILE
// Frame counter. drawForeground runs once per QGraphicsView paint whatever the viewport
// widget is, which is what makes it survive MEEGRAM_GL_VIEWPORT. fps is the delta in its
// calls between two dumps over the 5s dump interval; its avg is meaningless, the scope
// wraps a call that does nothing. No Q_OBJECT, for the same reason SceneTeardown has
// none: main.cpp stays out of moc.
class ProfiledView : public QDeclarativeView
{
protected:
    void drawForeground(QPainter *painter, const QRectF &rect) override
    {
        MEEGRAM_SCOPE("frame");
        QDeclarativeView::drawForeground(painter, rect);
    }
};

using Viewer = ProfiledView;
#else
using Viewer = QDeclarativeView;
#endif

// Drops the QML scene when the window is closed, leaving TDLib and NotificationManager
// running in the same process. Measured on device: the scene and its pixmap caches are
// 39.2 MiB of a 78.2 MiB resident set, and minimising returns none of it - the platform
// hands nothing back on its own (docs/restructuring.md).
//
// What this is for right now is one measurement: whether tearing the scene down from a
// fully built app actually returns that 39.2 MiB, or leaves a heap too fragmented to
// give it up. A headless process reaches 39.9 MiB by never allocating the scene, which
// proves the floor exists - not that this path can get back down to it.
//
// Gated on MEEGRAM_KEEPALIVE because it deliberately breaks closing the app: there is no
// rebuild-on-reactivate path yet, so once the scene is gone the process has to be killed.
// Building that before knowing whether the teardown even frees anything would be work
// thrown away if the answer is "it fragments".
//
// The QDeclarativeView is kept alive and only its source cleared. Destroying and
// recreating it would return roughly 4.6 MiB more - the "view+gl" marker - at the cost of
// a heap-allocated view whose lifetime spans the teardown. Every crash in this codebase
// so far has been object lifetime; that is a bad trade for 4.6 MiB.
class SceneTeardown : public QObject
{
public:
    explicit SceneTeardown(QDeclarativeView *view)
        : QObject(view)
        , m_view(view)
    {
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        // Posted rather than run inline: the close event is still being delivered to the
        // view, and setSource(QUrl()) destroys the root item out from under it. This
        // defers the teardown to a later trip through the event loop, once the close has
        // finished. No Q_OBJECT needed for either half, which keeps main.cpp out of moc.
        if (object == m_view && event->type() == QEvent::Close)
            QCoreApplication::postEvent(this, new QEvent(QEvent::User));

        return QObject::eventFilter(object, event);
    }

    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::User)
            return QObject::event(event);

        MEEGRAM_MARK("teardown-before");

        m_view->setSource(QUrl());
        m_view->engine()->clearComponentCache();

        // Clears what Qt's widget layer cached. Note this does NOT reach the decoded
        // avatars and stickers: ChatPhotoProvider and StickerProvider are stateless and
        // hand back a QImage, which QML1 keeps in its own QDeclarativePixmapStore - and
        // that store has no public flush in Qt 4.7. Dropping the scene releases its
        // references and the store trims unreferenced entries over its cost limit, but
        // not on demand.
        //
        // Which makes the measurement two-valued rather than one: if RSS does not fall,
        // the pixmap store is the first suspect, and that is a different problem from a
        // fragmented heap with a different fix.
        QPixmapCache::clear();

        MEEGRAM_MARK("teardown-after");

        // Measured: the teardown above returns 52 KiB of a 28 MiB scene. The objects are
        // destroyed, but every QML allocation is small enough to come from a glibc arena
        // rather than mmap, so free() hands nothing back to the kernel and RSS - which is
        // what the low-memory killer reads - does not move. /proc smaps confirms it:
        // 23.4 MiB resident in [heap] with the scene gone.
        //
        // malloc_trim walks the arenas and madvises free pages away. It is the difference
        // between "freed" and "returned", and it decides the architecture:
        //
        //   RSS falls  -> allocator retention, single process is viable, one line fixes it
        //   RSS flat   -> something still holds the memory, and only process exit
        //                 releases it, which is the service architecture's whole argument
#ifdef __GLIBC__
        malloc_trim(0);
        MEEGRAM_MARK("teardown-trimmed");
#endif

        return true;
    }

private:
    QDeclarativeView *m_view;
};

#ifdef MEEGRAM_PROFILE
// Breaks the "pre-setsource" -> "busy-shown" gap down per file. That gap is the largest
// single phase of a cold start (1877 ms of 4180 ms, measured on device), and Qt 4.7.4 has
// no way to precompute any of it: no Qt Quick Compiler (5.8), no disk cache (5.11), not
// even QDeclarativeComponent::Asynchronous (4.8). QDeclarativeCompiledData lives in the
// engine and dies with it. So the only levers are compiling less and compiling later, and
// which of those is worth pulling depends on where the time is.
//
// Here rather than in a tools/ binary because every interesting file starts with
// "import MyComponent 1.0" - the module registered above, which exists only inside this
// process. A standalone harness cannot resolve it, so main.qml and MainPage.qml, the two
// files that matter, are exactly the two it could not measure.
//
// A fresh QDeclarativeEngine per file is the point: the component cache is per-engine, so
// one shared engine would charge the first file for the whole MeeGo Components import and
// hand every later file a free ride. Each row is therefore "this file, cold, including its
// imports" - subtract the [imports only] row for the file's own share.
//
// What a fresh engine does *not* reset is the module plugin, which is dlopen()ed once per
// process. Pass 1 carries it, pass 2 does not; the difference is load cost that no
// restructuring of our own QML can remove.
//
// Compile only, never create(): instantiation needs a view for most of these roots, and
// separating the two is the whole question. If the scene time turns out to be
// instantiation rather than compilation, then precompiled QML would not have helped even
// if this Qt had it.
void runQmlBench(int passes)
{
    // Both levels: entryList does not recurse, and components/ holds MessageDelegate,
    // EmojiPicker and ChatItem, which are not small. Names are kept relative to :/qml so
    // the url below is the same shape for either.
    //
    // No name filter and no Filters argument. Asking for ("*.qml", QDir::Files) returned
    // exactly one of the nineteen top-level files on device, and QResourceFileEngine's
    // handling of either argument is not something this can verify from here - so it takes
    // the unfiltered listing, which one call cannot get wrong, and selects by suffix in
    // C++. The counts are printed for the same reason: a listing that silently returns one
    // entry produced a table that looked complete and was not.
    QStringList files;

    const QStringList roots = QDir(":/qml").entryList();
    const QStringList componentNames = QDir(":/qml/components").entryList();

    for (const QString &name : roots)
    {
        if (name.endsWith(".qml"))
            files.append(name);
    }

    for (const QString &name : componentNames)
    {
        if (name.endsWith(".qml"))
            files.append("components/" + name);
    }

    files.sort();

    // The names, not just the counts. Two runs were spent reasoning about why a listing
    // that reports the right number of entries yields the wrong number of files; the
    // entries themselves settle it in one run and should have been the first thing
    // printed. Quoted so trailing whitespace or an empty name is visible.
    std::fprintf(stderr, "---- MEEGRAM QMLBENCH ---- %d entries in :/qml, %d in :/qml/components, %d .qml selected\n",
                 roots.size(), componentNames.size(), files.size());

    for (const QString &name : roots)
        std::fprintf(stderr, "      root entry      \"%s\"\n", qPrintable(name));

    for (const QString &name : componentNames)
        std::fprintf(stderr, "      component entry \"%s\"\n", qPrintable(name));

    // The imports arm, built from main.qml's own import block so it cannot drift from what
    // the app actually asks for. QtObject rather than Item: no visual parent needed.
    QByteArray stub;
    {
        QFile mainQml(":/qml/main.qml");
        if (mainQml.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            while (!mainQml.atEnd())
            {
                const QByteArray line = mainQml.readLine().trimmed();

                if (line.startsWith("import "))
                    stub += line + "\n";
                // Imports precede the root object, so the first line that is neither an
                // import, a comment nor blank means there are no more.
                else if (!line.isEmpty() && !line.startsWith("//"))
                    break;
            }
        }
        stub += "\nQtObject {}\n";
    }

    for (int pass = 1; pass <= passes; ++pass)
    {
        std::fprintf(stderr, "---- MEEGRAM QMLBENCH ---- pass %d/%d%s\n", pass, passes,
                     pass == 1 ? " (includes one-off module plugin load)" : " (plugin already loaded)");

        {
            QDeclarativeEngine engine;
            const auto start = std::chrono::steady_clock::now();
            QDeclarativeComponent component(&engine);
            // setData, not a file: the stub has no url of its own. The base url still has
            // to point at :/qml so "import \"components\"" resolves.
            component.setData(stub, QUrl("qrc:/qml/__imports_only.qml"));
            const double ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0;

            std::fprintf(stderr, "  %-34s %9.1f ms%s\n", "[imports only]", ms, component.isError() ? "  ERROR" : "");

            if (component.isError())
            {
                for (const QDeclarativeError &error : component.errors())
                    std::fprintf(stderr, "      %s\n", qPrintable(error.toString()));
            }
        }

        for (const QString &name : files)
        {
            QDeclarativeEngine engine;

            const auto start = std::chrono::steady_clock::now();
            QDeclarativeComponent component(&engine, QUrl("qrc:/qml/" + name));
            const double ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0;

            // A file that fails to compile is not a fast file; say so rather than
            // reporting the 2 ms it took to give up.
            std::fprintf(stderr, "  %-34s %9.1f ms%s\n", qPrintable(name), ms, component.isError() ? "  ERROR" : "");

            if (component.isError())
            {
                for (const QDeclarativeError &error : component.errors())
                    std::fprintf(stderr, "      %s\n", qPrintable(error.toString()));
            }
        }
    }

    std::fflush(stderr);
}
#endif

}  // namespace

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    // Before QApplication, so whatever Qt reports while it is being built is already going
    // to the file rather than to the stderr invoker discards.
    openLog("meegram.log");
    qInstallMsgHandler(logMessage);

    // Time zero for every marker below. Everything before it - the dynamic linker
    // relocating a ~40 MB mostly-static binary and demand-paging it off eMMC - is
    // invisible from in here; see the note in ScopeTimer.hpp for how to bracket it.
    MEEGRAM_MARK("main-entry");

    QApplication app(argc, argv);

    // See the note in ScopeTimer.hpp. These four straddle the Qt / QML / TDLib
    // boundaries, so consecutive deltas attribute the resident set between them.
    MEEGRAM_MARK("qt-app");

    QCoreApplication::setApplicationName(AppName);
    QCoreApplication::setApplicationVersion(AppVersion);
    QCoreApplication::setOrganizationName("insider");

    QFontDatabase::addApplicationFont(":/fonts/Icons.ttf");
    // NotoEmoji-Regular.ttf was loaded here but is in neither resources/fonts.qrc nor
    // on disk, so the call always failed silently. Emoji are rendered as <img> tags by
    // Utils::replaceEmoji, not by a font.
    // NotoSansSymbols is kept: nothing names it via font.family, but Qt uses it as an
    // automatic fallback for glyphs missing from the UI font.
    QFontDatabase::addApplicationFont(":/fonts/NotoSansSymbols-Regular.ttf");

    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));

    // Splits font loading off from type registration: addApplicationFont parses a TTF
    // and rebuilds the font database, which is the one call between here and the view
    // that does real work. The 45 qmlRegister calls below are table inserts.
    MEEGRAM_MARK("fonts");

    qRegisterMetaType<qlonglong>("qlonglong");

    qRegisterMetaType<QList<qlonglong>>("QList<qlonglong>");

    qRegisterMetaType<Chat::Type>("Chat::Type");

    qRegisterMetaType<QModelIndex>("QModelIndex");

    qmlRegisterType<LottieAnimation>("MyComponent", 1, 0, "LottieAnimation");
    qmlRegisterType<QrCodeItem>("MyComponent", 1, 0, "QrCode");
    qmlRegisterType<VoiceNote>("MyComponent", 1, 0, "VoiceNote");

    qmlRegisterUncreatableType<Client>("MyComponent", 1, 0, "Client", "Client cannot be created from QML.");
    qmlRegisterUncreatableType<Authorization>("MyComponent", 1, 0, "Authorization", "Authorization cannot be created from QML.");
    qmlRegisterUncreatableType<Locale>("MyComponent", 1, 0, "Locale", "Locale cannot be created from QML.");
    qmlRegisterUncreatableType<Settings>("MyComponent", 1, 0, "Settings", "Settings cannot be created from QML.");
    qmlRegisterUncreatableType<ChatManager>("MyComponent", 1, 0, "ChatManager", "ChatManager cannot be created from QML.");
    qmlRegisterUncreatableType<StorageManager>("MyComponent", 1, 0, "StorageManager", "BasicGroup cannot be created from QML.");
    qmlRegisterUncreatableType<LanguagePackInfoModel>("MyComponent", 1, 0, "LanguagePackInfoModel", "LanguagePackInfoModel cannot be created from QML.");

    qmlRegisterUncreatableType<BasicGroup>("MyComponent", 1, 0, "BasicGroup", "BasicGroup cannot be created from QML.");
    qmlRegisterUncreatableType<Chat>("MyComponent", 1, 0, "Chat", "Chat cannot be created from QML.");
    qmlRegisterUncreatableType<ChatManager>("MyComponent", 1, 0, "ChatInfo", "ChatInfo cannot be created from QML.");
    qmlRegisterUncreatableType<ChatPosition>("MyComponent", 1, 0, "ChatPosition", "ChatPosition cannot be created from QML.");
    qmlRegisterUncreatableType<File>("MyComponent", 1, 0, "File", "File cannot be created from QML.");
    qmlRegisterUncreatableType<Message>("MyComponent", 1, 0, "Message", "Message cannot be created from QML.");
    qmlRegisterUncreatableType<Supergroup>("MyComponent", 1, 0, "Supergroup", "Supergroup cannot be created from QML.");
    qmlRegisterUncreatableType<SupergroupFullInfo>("MyComponent", 1, 0, "SupergroupFullInfo", "SupergroupFullInfo cannot be created from QML.");
    qmlRegisterUncreatableType<User>("MyComponent", 1, 0, "User", "User cannot be created from QML.");

    qmlRegisterUncreatableType<MessageText>("MyComponent", 1, 0, "MessageText", "MessageText cannot be created from QML.");
    qmlRegisterUncreatableType<MessageAnimation>("MyComponent", 1, 0, "MessageAnimation", "MessageAnimation cannot be created from QML.");
    qmlRegisterUncreatableType<MessageAudio>("MyComponent", 1, 0, "MessageAudio", "MessageAudio cannot be created from QML.");
    qmlRegisterUncreatableType<MessageDocument>("MyComponent", 1, 0, "MessageDocument", "MessageDocument cannot be created from QML.");
    qmlRegisterUncreatableType<MessagePhoto>("MyComponent", 1, 0, "MessagePhoto", "MessagePhoto cannot be created from QML.");
    qmlRegisterUncreatableType<MessageSticker>("MyComponent", 1, 0, "MessageSticker", "MessageSticker cannot be created from QML.");
    qmlRegisterUncreatableType<MessageVideo>("MyComponent", 1, 0, "MessageVideo", "MessageVideo cannot be created from QML.");
    qmlRegisterUncreatableType<MessageVideoNote>("MyComponent", 1, 0, "MessageVideoNote", "MessageVideoNote cannot be created from QML.");
    qmlRegisterUncreatableType<MessageVoiceNote>("MyComponent", 1, 0, "MessageVoiceNote", "MessageVoiceNote cannot be created from QML.");
    qmlRegisterUncreatableType<MessageLocation>("MyComponent", 1, 0, "MessageLocation", "MessageLocation cannot be created from QML.");
    qmlRegisterUncreatableType<MessageVenue>("MyComponent", 1, 0, "MessageVenue", "MessageVenue cannot be created from QML.");
    qmlRegisterUncreatableType<MessageContact>("MyComponent", 1, 0, "MessageContact", "MessageContact cannot be created from QML.");
    qmlRegisterUncreatableType<MessageAnimatedEmoji>("MyComponent", 1, 0, "MessageAnimatedEmoji", "MessageAnimatedEmoji cannot be created from QML.");
    qmlRegisterUncreatableType<MessagePoll>("MyComponent", 1, 0, "MessagePoll", "MessagePoll cannot be created from QML.");
    qmlRegisterUncreatableType<MessageInvoice>("MyComponent", 1, 0, "MessageInvoice", "MessageInvoice cannot be created from QML.");
    qmlRegisterUncreatableType<MessageCall>("MyComponent", 1, 0, "MessageCall", "MessageCall cannot be created from QML.");
    qmlRegisterUncreatableType<MessageService>("MyComponent", 1, 0, "MessageService", "MessageService cannot be created from QML.");

    Viewer viewer;

#ifdef MEEGRAM_GL_VIEWPORT
    // QDeclarativeView is a QGraphicsView, so without a GL viewport every repaint,
    // clip region and offscreen composite is rasterised on the CPU. Backing it with
    // a QGLWidget is the standard Harmattan configuration for the SGX530.
    //
    // A GL viewport cannot do partial updates, hence FullViewportUpdate; the opaque
    // attributes stop Qt from clearing the background before each frame.
    // Configure with -DMEEGRAM_GL_VIEWPORT=OFF to A/B this against software paint.
    auto *glWidget = new QGLWidget;
    glWidget->setAutoFillBackground(false);

    viewer.setViewport(glWidget);
    viewer.setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    viewer.setAttribute(Qt::WA_OpaquePaintEvent);
    viewer.setAttribute(Qt::WA_NoSystemBackground);
    viewer.viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewer.viewport()->setAttribute(Qt::WA_NoSystemBackground);
#endif

    MEEGRAM_MARK("view+gl");

    AppManager appManager;
    Utils utils;

    // TDLib exists from here, but its databases are not open yet - it grows over the
    // sync that follows, so this marker is its floor, not its cost. The 5-second PROF
    // table carries the rss= that tracks the rest.
    MEEGRAM_MARK("tdlib-client");

    // MEEGRAM_HEADLESS=1 syncs TDLib without ever building the QML scene, so its
    // steady-state RSS can be read on its own. That is the one figure the startup
    // markers cannot produce: everything after "qml-scene" is TDLib opening its
    // databases and QML decoding avatars and emoji into pixmap caches, mixed, with no
    // way to tell the two apart from a single total.
    //
    // initialize() has to be called explicitly here because the QML scene is normally
    // what calls it (resources/qml/main.qml:121) - skipping setSource alone would
    // leave TDLib idle and measure nothing.
    //
    // The already-built viewer and GL widget stay in the number; subtract the measured
    // "view+gl" reading to get TDLib alone. No periodic sampler on this path either -
    // the PROF table only dumps when an instrumented scope runs, and with no QML none
    // do. Poll it from outside instead:
    //
    //   while :; do grep VmRSS /proc/$(pidof meegram)/status; sleep 5; done
    if (qgetenv("MEEGRAM_HEADLESS") == "1")
    {
        appManager.initialize();
        MEEGRAM_MARK("headless-tdlib-start");
        return app.exec();
    }

#ifdef MEEGRAM_PROFILE
    // MEEGRAM_QML_BENCH=N compiles every .qml on its own engine, N passes, and exits
    // without building the scene or touching TDLib. Here rather than earlier because the
    // qmlRegisterType calls above are what make "import MyComponent 1.0" resolvable, and
    // every file worth measuring starts with one.
    if (const int passes = qgetenv("MEEGRAM_QML_BENCH").toInt(); passes > 0)
    {
        runQmlBench(passes);
        return 0;
    }
#endif

    app.installTranslator(appManager.locale());

    viewer.rootContext()->setContextProperty("appManager", &appManager);
    viewer.rootContext()->setContextProperty("utils", &utils);

    viewer.rootContext()->setContextProperty("AppVersion", AppVersion);

    viewer.engine()->addImageProvider("chatPhoto", new ChatPhotoProvider);
    // Static stickers only; Qt 4.7 cannot decode WebP on its own.
    viewer.engine()->addImageProvider("sticker", new StickerProvider);

    QObject::connect(viewer.engine(), SIGNAL(quit()), &viewer, SLOT(close()));

    // MEEGRAM_KEEPALIVE=1 keeps the process alive when the window closes and drops the
    // scene instead. setQuitOnLastWindowClosed(false) is the switch that matters: Qt
    // exiting on last-window-close is the actual mechanism that kills the TDLib
    // connection today, which is the problem docs/restructuring.md set out to solve.
    //
    // Owned by the view, so it is destroyed with it - no lifetime to manage.
    if (qgetenv("MEEGRAM_KEEPALIVE") == "1")
    {
        app.setQuitOnLastWindowClosed(false);
        viewer.installEventFilter(new SceneTeardown(&viewer));
    }

    viewer.setResizeMode(QDeclarativeView::SizeRootObjectToView);

    MEEGRAM_MARK("pre-setsource");

    viewer.setSource(QUrl("qrc:/qml/main.qml"));

    // The QML scene is built by setSource, so this delta is the number the whole
    // exercise turns on: what a resident UI costs over a headless TDLib. In time it is
    // Qt 4.7 parsing and compiling 31 .qml files with no compiled-QML cache anywhere -
    // the prime suspect for the wall-clock half of the same marker.
    MEEGRAM_MARK("qml-scene");

    viewer.showFullScreen();

    // Split from the marker above because they fail differently: a slow setSource is
    // QML compilation, a slow showFullScreen is the window manager and the first GL
    // buffer swap. Both land before anything is on screen and neither is separable
    // from outside the process.
    MEEGRAM_MARK("shown");

    return app.exec();
}
