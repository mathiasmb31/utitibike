
#include <QGuiApplication>
#include <QQuickView>
#include <QUrl>
#include <QScreen>
#include <QQmlContext>
#include <QQmlEngine>
#include <QDebug>
#include <QTranslator>
#include <QLocale>
#include <QDir>
#include <QStandardPaths>

#include "tilecache.h"
#include "tilemapitem.h"
#include "brouterclient.h"
#include "gpstracker.h"
#include "routerecorder.h"
#include "routehistory.h"
#include "favorites.h"
#include "urlopener.h"
#include "haptics.h"
#include "appstate.h"
#include "displaykeeper.h"
#include "geoutils.h"
#include "viewstate.h"
#include "appdata.h"
#include "searchclient.h"

int main(int argc, char *argv[])
{
    // Nom d'application = nom du paquet click (mathias.utitibike) pour que
    // QStandardPaths retourne des chemins couverts par les droits par défaut
    // du template AppArmor (@{HOME}/.local/share/@{APP_PKGNAME}/** etc.) —
    // évite les redflags read_path/write_path dans le click review.
    // Pas de organizationName : sinon Qt ajoute un sous-répertoire en plus.
    // IMPORTANT : setApplicationName AVANT le constructeur QGuiApplication,
    // sinon le constructeur initialise applicationName depuis argv[0]
    // ("utitibike") et QStandardPaths cache ce chemin — AppDataLocation
    // retournerait ~/.local/share/utitibike (hors AppArmor) au lieu de
    // ~/.local/share/mathias.utitibike (couvert par le template par défaut).
    QCoreApplication::setApplicationName(QStringLiteral("mathias.utitibike"));
    QCoreApplication::setOrganizationName(QStringLiteral(""));

    QGuiApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << "  Utitibike v" UTITIBIKE_VERSION " — Itinéraires vélo";
    qDebug() << "  Carte OSM · BRouter · GPS · Export GPX";
    qDebug() << "========================================";
    // Sauvegarder le chemin AVANT que le plugin QPA ne change applicationName
    g_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qDebug() << "appDataDir:" << g_appDataDir;

    // Internationalisation : charge utitibike_<locale>.qm selon la langue du
    // système (ex. fr_FR -> essaie utitibike_fr_FR.qm puis utitibike_fr.qm).
    // Sans traduction trouvée, les chaînes source (français) sont utilisées.
    QTranslator translator;
    const QString sysLocale = QLocale::system().name();
    const QString appDir = QCoreApplication::applicationDirPath();
    if (translator.load(QStringLiteral("utitibike_") + sysLocale, appDir)) {
        app.installTranslator(&translator);
        qDebug() << "utitibike translation loaded for locale" << sysLocale;
    } else {
        qDebug() << "utitibike locale" << sysLocale << "- no translation file, keeping source language";
    }

    qmlRegisterType<TileCache>("Utitibike", 1, 0, "TileCache");
    qmlRegisterType<TileMapItem>("Utitibike", 1, 0, "TileMapItem");
    qmlRegisterType<BrouterClient>("Utitibike", 1, 0, "BrouterClient");
    qmlRegisterSingletonType<GeoUtils>("Utitibike", 1, 0, "GeoUtils",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new GeoUtils; });

    // Main.qml's root is a MainView (an Item, not a Window). QQmlApplicationEngine
    // creates no window for such a root, so the app would never get a Mir surface
    // and would stay invisible. QQuickView hosts the root item in a real window.
    UrlOpener urlOpener;
    Haptics haptics;
    DisplayKeeper displayKeeper;
    GpsTracker gpsTracker;
    RouteRecorder routeRecorder(&gpsTracker);
    RouteHistory routeHistory;
    Favorites favorites;
    ViewState viewState;
    SearchClient searchClient;
    BrouterClient brouter;
    QQuickView view;
    // Redirige XDG_CACHE_HOME vers ~/.cache/mathias.utitibike/ pour que le
    // QML disk cache et le shader cache Qt soient couverts par les droits
    // AppArmor par défaut (@{HOME}/.cache/@{APP_PKGNAME}/**). Sans ça, le
    // QML engine utilise ~/.cache/<executable>/qmlcache/ (hors droits).
    const QString xdgCache = QDir::home().absolutePath()
                             + QStringLiteral("/.cache/mathias.utitibike");
    QDir().mkpath(xdgCache);
    qputenv("XDG_CACHE_HOME", xdgCache.toUtf8());
    AppState appState(&view);
    view.rootContext()->setContextProperty(QStringLiteral("urlOpener"), &urlOpener);
    view.rootContext()->setContextProperty(QStringLiteral("haptics"), &haptics);
    view.rootContext()->setContextProperty(QStringLiteral("appState"), &appState);
    view.rootContext()->setContextProperty(QStringLiteral("gpsTracker"), &gpsTracker);
    view.rootContext()->setContextProperty(QStringLiteral("routeRecorder"), &routeRecorder);
    view.rootContext()->setContextProperty(QStringLiteral("routeHistory"), &routeHistory);
    view.rootContext()->setContextProperty(QStringLiteral("favorites"), &favorites);
    view.rootContext()->setContextProperty(QStringLiteral("viewState"), &viewState);
    view.rootContext()->setContextProperty(QStringLiteral("searchClient"), &searchClient);
    // brouter en context property pour que HistoryPage puisse appeler
    // loadGpxData() directement (pageStack.get(0) n'est pas fiable).
    view.rootContext()->setContextProperty(QStringLiteral("brouter"), &brouter);
    // Version de l'app (définie dans CMakeLists.txt) pour le QML
    view.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                           QStringLiteral(UTITIBIKE_VERSION));
    // displayKeeper retient l'écran allumé pendant l'enregistrement GPS / la
    // navigation (keep-display-on) ; il est lâché à la sortie de l'app.
    view.rootContext()->setContextProperty(QStringLiteral("displayKeeper"), &displayKeeper);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &displayKeeper, &DisplayKeeper::release);
    // Dernier filet : si un enregistrement est en cours à la fermeture, le
    // parcours est sauvé (sinon il serait perdu, l'auto-save n'a lieu que
    // toutes les 60 s).
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &routeRecorder, [&routeRecorder]() {
        if (routeRecorder.recording())
            routeRecorder.saveGpx();
    });
    // The root item follows the window, and the window takes the whole screen:
    // the UI adapts to every phone's resolution.
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (view.status() == QQuickView::Error || view.rootObject() == nullptr)
        return -1;
    const QScreen *screen = app.primaryScreen();
    if (screen && !screen->size().isEmpty())
        view.resize(screen->size());
    view.showFullScreen();
    qDebug() << "utitibike window" << view.width() << "x" << view.height()
             << "screen" << (screen ? screen->size() : QSize());

    return app.exec();
}

