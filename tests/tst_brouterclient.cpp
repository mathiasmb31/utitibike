#include <QtTest>
#include <QSignalSpy>
#include <QVariantList>
#include <QVariantMap>
#include "brouterclient.h"

// Tests de BrouterClient : parsing GPX (loadGpxData), validation des
// waypoints invalides dans requestRoute (0,0 / NaN / hors limites).
// Pas de test réseau : on teste uniquement la logique locale.
class TestBrouterClient : public QObject
{
    Q_OBJECT
private slots:
    void loadGpxValid();
    void loadGpxWithTimestamps();
    void loadGpxInvalidXml();
    void loadGpxEmpty();
    void loadGpxNoTrackPoints();
    void loadGpxMultipleTrkSeg();
    void requestRouteRejectsZeroZero();
    void requestRouteRejectsNaN();
    void requestRouteRejectsOutOfRange();
    void requestRouteRejectsTooFewWaypoints();
    void defaultProfile();
    void defaultUphillCost();
};

static QVariantList makeWaypoint(double lat, double lon)
{
    QVariantMap m;
    m.insert("lat", lat);
    m.insert("lon", lon);
    return { m };
}

void TestBrouterClient::loadGpxValid()
{
    BrouterClient client;
    QSignalSpy readySpy(&client, &BrouterClient::routeReady);
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);

    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<gpx version=\"1.1\" creator=\"test\">"
        "<trk><name>Test</name><trkseg>"
        "<trkpt lat=\"43.6044\" lon=\"1.4442\"><ele>150</ele></trkpt>"
        "<trkpt lat=\"43.6100\" lon=\"1.4500\"><ele>160</ele></trkpt>"
        "<trkpt lat=\"43.6200\" lon=\"1.4600\"><ele>155</ele></trkpt>"
        "</trkseg></trk>"
        "</gpx>");

    client.loadGpxData(gpx);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(readySpy.count(), 1);
    const QVariantList points = readySpy.at(0).at(0).toList();
    QCOMPARE(points.size(), 3);
    // Distance > 0 (3 points espacés)
    const double dist = readySpy.at(0).at(1).toDouble();
    QVERIFY(dist > 0.0);
    // D+ = 160-150 = 10m (montée), puis descente ignorée
    const double ascent = readySpy.at(0).at(2).toDouble();
    QCOMPARE(ascent, 10.0);
    // lastGpx contient le texte original
    QVERIFY(client.lastGpx().contains("trkpt"));
}

void TestBrouterClient::loadGpxWithTimestamps()
{
    BrouterClient client;
    QSignalSpy readySpy(&client, &BrouterClient::routeReady);
    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>"
        "<trkpt lat=\"43.6000\" lon=\"1.4400\"><ele>100</ele>"
        "<time>2026-11-22T07:00:00.000Z</time></trkpt>"
        "<trkpt lat=\"43.6100\" lon=\"1.4500\"><ele>110</ele>"
        "<time>2026-11-22T07:00:10.000Z</time></trkpt>"
        "<trkpt lat=\"43.6200\" lon=\"1.4600\"><ele>115</ele>"
        "<time>2026-11-22T07:00:20.000Z</time></trkpt>"
        "</trkseg></trk></gpx>");
    client.loadGpxData(gpx);
    QCOMPARE(readySpy.count(), 1);
    const QVariantList points = readySpy.at(0).at(0).toList();
    QCOMPARE(points.size(), 3);
    const qint64 t0 = points.at(0).toMap().value(QStringLiteral("t")).toLongLong();
    const qint64 t2 = points.at(2).toMap().value(QStringLiteral("t")).toLongLong();
    QVERIFY(t0 > 0);
    QCOMPARE(t2 - t0, 20000LL);  // 20 s entre le 1er et le dernier point
}

void TestBrouterClient::loadGpxInvalidXml()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    client.loadGpxData("ceci n'est pas du XML");
    QCOMPARE(failSpy.count(), 1);
}

void TestBrouterClient::loadGpxEmpty()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    client.loadGpxData("");
    QCOMPARE(failSpy.count(), 1);
}

void TestBrouterClient::loadGpxNoTrackPoints()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg></trkseg></trk></gpx>");
    client.loadGpxData(gpx);
    QCOMPARE(failSpy.count(), 1);
}

void TestBrouterClient::loadGpxMultipleTrkSeg()
{
    // BRouter peut couper un itinéraire en plusieurs <trkseg> (ferry, tunnel).
    // parseGpx doit insérer un point NaN entre les segments pour que
    // visiblePath coupe proprement la ligne (évite une ligne droite artefact).
    BrouterClient client;
    QSignalSpy readySpy(&client, &BrouterClient::routeReady);
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);

    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<gpx version=\"1.1\" creator=\"test\">"
        "<trk><name>Test</name>"
        "<trkseg>"
        "<trkpt lat=\"43.6044\" lon=\"1.4442\"><ele>150</ele></trkpt>"
        "<trkpt lat=\"43.6100\" lon=\"1.4500\"><ele>160</ele></trkpt>"
        "</trkseg>"
        "<trkseg>"
        "<trkpt lat=\"43.7000\" lon=\"1.5000\"><ele>200</ele></trkpt>"
        "<trkpt lat=\"43.7100\" lon=\"1.5100\"><ele>210</ele></trkpt>"
        "</trkseg>"
        "</trk></gpx>");

    client.loadGpxData(gpx);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(readySpy.count(), 1);
    const QVariantList points = readySpy.at(0).at(0).toList();
    // 2 points segment 1 + 1 séparateur NaN + 2 points segment 2 = 5
    QCOMPARE(points.size(), 5);
    // Le point à l'index 2 doit être le séparateur NaN
    const QVariantMap sep = points.at(2).toMap();
    QVERIFY(!qIsFinite(sep.value("lat").toDouble()));
    QVERIFY(!qIsFinite(sep.value("lon").toDouble()));
    // Les vrais points sont aux index 0, 1, 3, 4
    QCOMPARE(points.at(0).toMap().value("lat").toDouble(), 43.6044);
    QCOMPARE(points.at(1).toMap().value("lat").toDouble(), 43.6100);
    QCOMPARE(points.at(3).toMap().value("lat").toDouble(), 43.7000);
    QCOMPARE(points.at(4).toMap().value("lat").toDouble(), 43.7100);

    // v1.0: le marqueur NaN ne doit PAS couper le guidage. Avant le correctif,
    // haversine(NaN)=0 élisait le séparateur comme « point le plus proche »
    // et déclenchait routeFinished() au premier updatePosition().
    QSignalSpy finishedSpy(&client, &BrouterClient::routeFinished);
    client.startNavigation();
    QVERIFY(client.navigation());
    client.updatePosition(43.6050, 1.4450);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(client.navigation());
}

void TestBrouterClient::requestRouteRejectsZeroZero()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    // Deux waypoints, dont un à (0,0) — doit être rejeté par la validation
    QVariantList wps;
    wps << makeWaypoint(43.6044, 1.4442)
        << makeWaypoint(0.0, 0.0);
    client.requestRoute(wps);
    QCOMPARE(failSpy.count(), 1);
    QVERIFY(!client.busy());  // pas de requête lancée
}

void TestBrouterClient::requestRouteRejectsNaN()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    QVariantList wps;
    wps << makeWaypoint(43.6044, 1.4442)
        << makeWaypoint(std::numeric_limits<double>::quiet_NaN(), 1.4442);
    client.requestRoute(wps);
    QCOMPARE(failSpy.count(), 1);
}

void TestBrouterClient::requestRouteRejectsOutOfRange()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    QVariantList wps;
    wps << makeWaypoint(43.6044, 1.4442)
        << makeWaypoint(91.0, 1.4442);  // lat > 90
    client.requestRoute(wps);
    QCOMPARE(failSpy.count(), 1);

    QSignalSpy failSpy2(&client, &BrouterClient::routeFailed);
    QVariantList wps2;
    wps2 << makeWaypoint(43.6044, 1.4442)
         << makeWaypoint(43.6044, 181.0);  // lon > 180
    client.requestRoute(wps2);
    QCOMPARE(failSpy2.count(), 1);
}

void TestBrouterClient::requestRouteRejectsTooFewWaypoints()
{
    BrouterClient client;
    QSignalSpy failSpy(&client, &BrouterClient::routeFailed);
    // Un seul waypoint valide — doit échouer (< 2 points)
    client.requestRoute(makeWaypoint(43.6044, 1.4442));
    QCOMPARE(failSpy.count(), 1);
    // Zéro waypoint
    client.requestRoute({});
    QCOMPARE(failSpy.count(), 2);
}

void TestBrouterClient::defaultProfile()
{
    BrouterClient client;
    QVERIFY(client.profile().isEmpty());  // vide par défaut, "trekking" côté QML
}

void TestBrouterClient::defaultUphillCost()
{
    BrouterClient client;
    QCOMPARE(client.uphillCost(), 50);  // valeur par défaut dans le constructeur
}

QTEST_GUILESS_MAIN(TestBrouterClient)
#include "tst_brouterclient.moc"
