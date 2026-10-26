
#include <QGuiApplication>
#include <QQuickView>
#include <QUrl>
#include <QScreen>
#include <QQmlContext>
#include <QDebug>
#include <QTranslator>
#include <QLocale>

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

#include <QQmlEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("utitibike"));
    app.setApplicationName(QStringLiteral("utitibike"));

    qDebug() << "========================================";
    qDebug() << "  Utitibike v" UTITIBIKE_VERSION " — Itinéraires vélo";
    qDebug() << "  Carte OSM · BRouter · GPS · Export GPX";
    qDebug() << "========================================";

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
    QQuickView view;
    AppState appState(&view);
    view.rootContext()->setContextProperty(QStringLiteral("urlOpener"), &urlOpener);
    view.rootContext()->setContextProperty(QStringLiteral("haptics"), &haptics);
    view.rootContext()->setContextProperty(QStringLiteral("appState"), &appState);
    view.rootContext()->setContextProperty(QStringLiteral("gpsTracker"), &gpsTracker);
    view.rootContext()->setContextProperty(QStringLiteral("routeRecorder"), &routeRecorder);
    view.rootContext()->setContextProperty(QStringLiteral("routeHistory"), &routeHistory);
    view.rootContext()->setContextProperty(QStringLiteral("favorites"), &favorites);
    view.rootContext()->setContextProperty(QStringLiteral("viewState"), &viewState);
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

