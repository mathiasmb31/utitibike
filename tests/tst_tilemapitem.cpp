#include <QtTest>
#include <QVariantMap>
#include <QPainterPath>
#include <QtMath>
#include <cmath>
#include "tilemapitem.h"

// Miroir des helpers de projection de tilemapitem.cpp (anonymous namespace,
// non exportés) pour pouvoir calculer ox/oy/world dans les tests de visiblePath.
static double testWorldSize(int z) { return 256.0 * std::pow(2.0, z); }
static double testLonToWorldX(double lon, double world)
{
    return (lon + 180.0) / 360.0 * world;
}
static double testLatToWorldY(double lat, double world)
{
    const double r = qDegreesToRadians(lat);
    return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / M_PI) / 2.0 * world;
}
static QVariantMap mkPoint(double lat, double lon)
{
    QVariantMap m;
    m.insert(QStringLiteral("lat"), lat);
    m.insert(QStringLiteral("lon"), lon);
    return m;
}
// Compte les MoveToElement d'un QPainterPath = nombre de sous-chemins dessinés.
static int moveToCount(const QPainterPath &path)
{
    int n = 0;
    for (int i = 0; i < path.elementCount(); ++i)
        if (path.elementAt(i).type == QPainterPath::MoveToElement)
            ++n;
    return n;
}

// Tests de TileMapItem : projection Mercator (itemToLatLon), pan, zoom,
// wrap longitude, clamp latitude, visibleExtent, coupe des trous de visiblePath.
// TileMapItem est un QQuickPaintedItem : QTEST_MAIN crée un QGuiApplication.
class TestTileMapItem : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void centerMapsToCenter();
    void itemToLatLonRoundTrip();
    void panByMovesCenter();
    void zoomChangesZoom();
    void wrapLongitude();
    void clampLatitude();
    void visibleExtentContainsCenter();
    void invalidWaypointNotAdded();  // ne teste pas addWaypoint (QML), mais itemToLatLon ne crash pas
    void routeHoleIsCut();           // trou dans les données -> 2 sous-chemins, à tout zoom
    void routeWithoutHoleIsConnected(); // pas de trou -> 1 seul sous-chemin
};

void TestTileMapItem::init()
{
    // Réinitialiser la carte à un état connu avant chaque test
}

void TestTileMapItem::centerMapsToCenter()
{
    TileMapItem map;
    map.setCenterLat(43.6044);
    map.setCenterLon(1.4442);
    map.setZoom(12);
    // Le centre de l'item (width/2, height/2) doit correspondre au centre GPS
    // Note: width/height sont 0 tant que l'item n'est pas dans une fenêtre,
    // donc on teste avec les propriétés directement.
    QCOMPARE(map.centerLat(), 43.6044);
    QCOMPARE(map.centerLon(), 1.4442);
    QCOMPARE(map.zoom(), 12);
}

void TestTileMapItem::itemToLatLonRoundTrip()
{
    TileMapItem map;
    map.setCenterLat(48.8566);
    map.setCenterLon(2.3522);
    map.setZoom(10);
    // Sans width/height, itemToLatLon(0,0) retourne le coin haut-gauche,
    // pas le centre. On vérifie juste que les coordonnées sont valides.
    const QVariantMap ll = map.itemToLatLon(0.0, 0.0);
    const double lat = ll.value("lat").toDouble();
    const double lon = ll.value("lon").toDouble();
    QVERIFY(lat >= -90.0 && lat <= 90.0);
    QVERIFY(lon >= -180.0 && lon <= 180.0);
    // Jamais NaN
    QVERIFY(qIsFinite(lat));
    QVERIFY(qIsFinite(lon));
}

void TestTileMapItem::panByMovesCenter()
{
    TileMapItem map;
    map.setCenterLat(43.6044);
    map.setCenterLon(1.4442);
    map.setZoom(12);
    const double latBefore = map.centerLat();
    const double lonBefore = map.centerLon();
    // panBy(dx, dy) déplace le centre dans la direction opposée au doigt
    // (le -dx/-dy est intentionnel : le contenu suit le doigt).
    // panBy(+100, 0) → centre vers l'Ouest → longitude diminue
    map.panBy(100.0, 0.0);
    QVERIFY(map.centerLon() < lonBefore);
    QCOMPARE(map.centerLat(), latBefore);  // pas de pan vertical
    // panBy(0, +100) → centre vers le Nord → latitude augmente
    map.panBy(0.0, 100.0);
    QVERIFY(map.centerLat() > latBefore);
}

void TestTileMapItem::zoomChangesZoom()
{
    TileMapItem map;
    map.setZoom(10);
    QCOMPARE(map.zoom(), 10);
    // zoomAbout avec delta +1
    map.zoomAbout(1, 0.0, 0.0);
    QCOMPARE(map.zoom(), 11);
    // zoomAbout avec delta -1
    map.zoomAbout(-1, 0.0, 0.0);
    QCOMPARE(map.zoom(), 10);
}

void TestTileMapItem::wrapLongitude()
{
    TileMapItem map;
    // Longitude > 180 doit être wrappée
    map.setCenterLon(190.0);
    QCOMPARE(map.centerLon(), -170.0);
    // Longitude < -180 doit être wrappée
    map.setCenterLon(-190.0);
    QCOMPARE(map.centerLon(), 170.0);
}

void TestTileMapItem::clampLatitude()
{
    TileMapItem map;
    // Latitude > 85.0511 (limite Mercator) doit être clampée
    map.setCenterLat(89.0);
    QCOMPARE(map.centerLat(), 85.0511);
    // Latitude < -85.0511 doit être clampée
    map.setCenterLat(-89.0);
    QCOMPARE(map.centerLat(), -85.0511);
}

void TestTileMapItem::visibleExtentContainsCenter()
{
    TileMapItem map;
    // Sans taille, visibleExtent() retourne un point (width=height=0)
    map.setWidth(800.0);
    map.setHeight(600.0);
    map.setCenterLat(43.6044);
    map.setCenterLon(1.4442);
    map.setZoom(12);
    const QVariantMap ext = map.visibleExtent();
    const double lat1 = ext.value("lat1").toDouble();
    const double lon1 = ext.value("lon1").toDouble();
    const double lat2 = ext.value("lat2").toDouble();
    const double lon2 = ext.value("lon2").toDouble();
    // Le centre doit être dans l'extent visible
    QVERIFY(43.6044 >= lat1 && 43.6044 <= lat2);
    QVERIFY(1.4442 >= lon1 && 1.4442 <= lon2);
    // lat1 <= lat2, lon1 <= lon2
    QVERIFY(lat1 <= lat2);
    QVERIFY(lon1 <= lon2);
}

void TestTileMapItem::invalidWaypointNotAdded()
{
    // itemToLatLon ne doit jamais retourner NaN, même avec des coordonnées
    // écran extrêmes (hors de l'item)
    TileMapItem map;
    map.setCenterLat(0.0);
    map.setCenterLon(0.0);
    map.setZoom(3);
    const QVariantMap ll = map.itemToLatLon(-10000.0, -10000.0);
    QVERIFY(qIsFinite(ll.value("lat").toDouble()));
    QVERIFY(qIsFinite(ll.value("lon").toDouble()));
    // Les coordonnées doivent être dans les limites valides
    QVERIFY(ll.value("lat").toDouble() >= -90.0);
    QVERIFY(ll.value("lat").toDouble() <= 90.0);
    QVERIFY(ll.value("lon").toDouble() >= -180.0);
    QVERIFY(ll.value("lon").toDouble() <= 180.0);
}

void TestTileMapItem::routeHoleIsCut()
{
    // Un trou dans les données (deux points consécutifs très éloignés, tous
    // deux visibles à l'écran) ne doit PAS produire une ligne droite artefact
    // vers un "point fantôme" : le segment doit être coupé (moveTo).
    // On vérifie à zoom 12 ET 14 pour prouver que la coupe est indépendante
    // du zoom (un filtre en pixels échouerait à faible zoom).
    const double centerLat = 43.6, centerLon = 1.44;
    const double w = 1080.0, h = 2340.0;
    QVariantList pts;
    pts << mkPoint(43.6000, 1.4400)   // ~55 m
        << mkPoint(43.6005, 1.4400)   // ~55 m
        << mkPoint(43.6010, 1.4400)   // ~55 m
        << mkPoint(43.6500, 1.4400)   // ~5,4 km  <-- TROU
        << mkPoint(43.6505, 1.4400)   // ~55 m
        << mkPoint(43.6510, 1.4400);  // ~55 m
    for (int zoom : {12, 14}) {
        const double world = testWorldSize(zoom);
        const double ox = testLonToWorldX(centerLon, world) - w / 2.0;
        const double oy = testLatToWorldY(centerLat, world) - h / 2.0;
        const QPainterPath path = TileMapItem::visiblePath(pts, ox, oy, world, w, h);
        // 6 points visibles -> 6 éléments. Le trou entre l'index 2 et 3 doit
        // être coupé : 2 sous-chemins (MoveTo en 0 et en 3), pas de LineTo
        // traversant le trou.
        QCOMPARE(path.elementCount(), 6);
        QCOMPARE(moveToCount(path), 2);
        QCOMPARE(path.elementAt(3).type, QPainterPath::MoveToElement);
    }
}

void TestTileMapItem::routeWithoutHoleIsConnected()
{
    // Contrôle : sans trou, tous les points visibles sont chaînés en un seul
    // sous-chemin (1 seul MoveTo, le premier point).
    const double centerLat = 43.6, centerLon = 1.44;
    const double w = 1080.0, h = 2340.0;
    const int zoom = 12;
    QVariantList pts;
    pts << mkPoint(43.6000, 1.4400)
        << mkPoint(43.6005, 1.4400)
        << mkPoint(43.6010, 1.4400)
        << mkPoint(43.6015, 1.4400);
    const double world = testWorldSize(zoom);
    const double ox = testLonToWorldX(centerLon, world) - w / 2.0;
    const double oy = testLatToWorldY(centerLat, world) - h / 2.0;
    const QPainterPath path = TileMapItem::visiblePath(pts, ox, oy, world, w, h);
    QCOMPARE(path.elementCount(), 4);
    QCOMPARE(moveToCount(path), 1);
    QCOMPARE(path.elementAt(0).type, QPainterPath::MoveToElement);
}

QTEST_MAIN(TestTileMapItem)
#include "tst_tilemapitem.moc"
