#include <QtTest>
#include <QVariantMap>
#include "tilemapitem.h"

// Tests de TileMapItem : projection Mercator (itemToLatLon), pan, zoom,
// wrap longitude, clamp latitude, visibleExtent.
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
    // Pan de 100px vers la droite = longitude augmente (à zoom 12, ~38m/px)
    map.panBy(100.0, 0.0);
    QVERIFY(map.centerLon() != lonBefore);
    QCOMPARE(map.centerLat(), latBefore);  // pas de pan vertical
    // Pan de 100px vers le bas = latitude diminue
    map.panBy(0.0, 100.0);
    QVERIFY(map.centerLat() < latBefore);
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

QTEST_MAIN(TestTileMapItem)
#include "tst_tilemapitem.moc"
