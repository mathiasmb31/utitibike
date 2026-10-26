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
    void loadGpxInvalidXml();
    void loadGpxEmpty();
    void loadGpxNoTrackPoints();
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
