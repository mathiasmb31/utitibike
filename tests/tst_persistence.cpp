#include <QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QVariantMap>
#include "favorites.h"
#include "routehistory.h"
#include "viewstate.h"

// Tests de persistance : Favorites, RouteHistory, ViewState.
// QStandardPaths::setTestModeEnabled(true) isole les écritures dans un
// dossier temporaire (pas dans ~/.local/share de la machine hôte).
class TestPersistence : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // Favorites
    void favoritesAddRemove();
    void favoritesIsFavorite();
    void favoritesClear();

    // RouteHistory
    void routeHistoryAddRemove();
    void routeHistoryClear();

    // ViewState
    void viewStateDefaults();
    void viewStateSetGet();
};

void TestPersistence::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestPersistence::cleanupTestCase()
{
    QStandardPaths::setTestModeEnabled(false);
}

// ── Favorites ────────────────────────────────────────────────────────

void TestPersistence::favoritesAddRemove()
{
    Favorites fav;
    fav.clear();
    QCOMPARE(fav.entries().size(), 0);

    fav.add("Maison", 43.6044, 1.4442, "domicile");
    QCOMPARE(fav.entries().size(), 1);
    QVariantMap entry = fav.entries().first().toMap();
    QCOMPARE(entry.value("name").toString(), QString("Maison"));
    QCOMPARE(entry.value("lat").toDouble(), 43.6044);
    QCOMPARE(entry.value("lon").toDouble(), 1.4442);
    QCOMPARE(entry.value("category").toString(), QString("domicile"));

    fav.add("Travail", 48.8566, 2.3522);
    QCOMPARE(fav.entries().size(), 2);

    fav.remove(0);
    QCOMPARE(fav.entries().size(), 1);
    QCOMPARE(fav.entries().first().toMap().value("name").toString(), QString("Travail"));

    fav.clear();
}

void TestPersistence::favoritesIsFavorite()
{
    Favorites fav;
    fav.clear();
    fav.add("Chez moi", 43.6044, 1.4442);
    // Même position exacte
    QVERIFY(fav.isFavorite(43.6044, 1.4442));
    // À ~5m (1e-4 degré ≈ 11m) — doit être considéré comme favori (seuil 10m)
    // 43.6044 + 0.00005 ≈ 5.5m en latitude
    QVERIFY(fav.isFavorite(43.60445, 1.4442));
    // À ~500m — ne doit PAS être favori
    QVERIFY(!fav.isFavorite(43.6094, 1.4442));
    fav.clear();
}

void TestPersistence::favoritesClear()
{
    Favorites fav;
    fav.add("A", 43.0, 1.0);
    fav.add("B", 44.0, 2.0);
    QCOMPARE(fav.entries().size(), 2);
    fav.clear();
    QCOMPARE(fav.entries().size(), 0);
}

// ── RouteHistory ─────────────────────────────────────────────────────

void TestPersistence::routeHistoryAddRemove()
{
    RouteHistory hist;
    hist.clear();
    QCOMPARE(hist.entries().size(), 0);

    QVariantList wps;
    QVariantMap wp1; wp1.insert("lat", 43.6044); wp1.insert("lon", 1.4442);
    QVariantMap wp2; wp2.insert("lat", 43.7000); wp2.insert("lon", 1.5000);
    wps << wp1 << wp2;

    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>"
        "<trkpt lat=\"43.6044\" lon=\"1.4442\"/>"
        "<trkpt lat=\"43.7000\" lon=\"1.5000\"/>"
        "</trkseg></trk></gpx>");

    hist.addEntry("Test Toulouse", 5000.0, 120.0, wps, gpx);
    QCOMPARE(hist.entries().size(), 1);

    QVariantMap entry = hist.entries().first().toMap();
    QCOMPARE(entry.value("name").toString(), QString("Test Toulouse"));
    QCOMPARE(entry.value("distanceM").toDouble(), 5000.0);
    QCOMPARE(entry.value("ascentM").toDouble(), 120.0);

    // Le GPX doit être externalisé et relisable
    const QString gpxLoaded = hist.gpxAt(0);
    QVERIFY(!gpxLoaded.isEmpty());
    QVERIFY(gpxLoaded.contains("trkpt"));

    // Les waypoints doivent être conservés
    const QVariantList wpsLoaded = hist.waypointsAt(0);
    QCOMPARE(wpsLoaded.size(), 2);

    hist.removeEntry(0);
    QCOMPARE(hist.entries().size(), 0);
    hist.clear();
}

void TestPersistence::routeHistoryClear()
{
    RouteHistory hist;
    hist.clear();
    QVariantList wps;
    hist.addEntry("A", 1000.0, 50.0, wps, "<gpx/>");
    hist.addEntry("B", 2000.0, 100.0, wps, "<gpx/>");
    QCOMPARE(hist.entries().size(), 2);
    hist.clear();
    QCOMPARE(hist.entries().size(), 0);
}

// ── ViewState ────────────────────────────────────────────────────────

void TestPersistence::viewStateDefaults()
{
    ViewState vs;
    // Défaut : Toulouse (comme TileMapItem)
    QCOMPARE(vs.centerLat(), 43.6044);
    QCOMPARE(vs.centerLon(), 1.4442);
    QCOMPARE(vs.zoom(), 12);
    // En mode test, pas de vue restaurée (fichier vide)
    QVERIFY(!vs.restored());
}

void TestPersistence::viewStateSetGet()
{
    ViewState vs;
    vs.setCenterLat(48.8566);
    vs.setCenterLon(2.3522);
    vs.setZoom(15);
    QCOMPARE(vs.centerLat(), 48.8566);
    QCOMPARE(vs.centerLon(), 2.3522);
    QCOMPARE(vs.zoom(), 15);
}

QTEST_GUILESS_MAIN(TestPersistence)
#include "tst_persistence.moc"
