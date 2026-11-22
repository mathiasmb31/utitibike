#include <QtTest>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>

#include "routehistory.h"

// Vérifie que profileAt() extrait bien tous les points avec leur altitude
// depuis le GPX d'une entrée d'historique (mini profil de la liste).
class TstProfile : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void profileFromGpx();
    void profileFromRealGpx();
};

void TstProfile::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    // history.json de test persiste entre les runs : on repart à zéro
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile::remove(base + QStringLiteral("/history.json"));
    QDir(base + QStringLiteral("/history")).removeRecursively();
}

static QString makeGpx()
{
    // 5 trkpt avec altitudes 100, 110, 105, 120, 115
    return QStringLiteral(
        "<?xml version=\"1.0\"?><gpx version=\"1.1\" creator=\"t\">"
        "<trk><name>t</name><trkseg>"
        "<trkpt lat=\"43.6000\" lon=\"1.4400\"><ele>100.0</ele><time>2026-11-03T10:00:00.000Z</time></trkpt>"
        "<trkpt lat=\"43.6010\" lon=\"1.4400\"><ele>110.0</ele><time>2026-11-03T10:00:05.000Z</time></trkpt>"
        "<trkpt lat=\"43.6020\" lon=\"1.4410\"><ele>105.0</ele><time>2026-11-03T10:00:10.000Z</time></trkpt>"
        "<trkpt lat=\"43.6030\" lon=\"1.4410\"><ele>120.0</ele><time>2026-11-03T10:00:15.000Z</time></trkpt>"
        "<trkpt lat=\"43.6040\" lon=\"1.4420\"><ele>115.0</ele><time>2026-11-03T10:00:20.000Z</time></trkpt>"
        "</trkseg></trk></gpx>");
}

void TstProfile::profileFromGpx()
{
    QStandardPaths::setTestModeEnabled(true);
    RouteHistory hist;
    hist.addEntry("test", 500.0, 10.0, QVariantList(), makeGpx());
    QCOMPARE(hist.entries().size(), 1);

    const QVariantList pts = hist.profileAt(0);
    QCOMPARE(pts.size(), 5);                       // tous les points extraits
    QCOMPARE(pts.at(0).toMap().value("ele").toDouble(), 100.0);
    QCOMPARE(pts.at(1).toMap().value("ele").toDouble(), 110.0);
    QCOMPARE(pts.at(3).toMap().value("ele").toDouble(), 120.0);
    QCOMPARE(pts.at(4).toMap().value("ele").toDouble(), 115.0);
    QCOMPARE(pts.at(2).toMap().value("lat").toDouble(), 43.6020);
    // v0.99.1: horodatage extrait pour la vitesse moyenne du profil
    const qint64 t1 = pts.at(1).toMap().value("t").toLongLong();
    QVERIFY(t1 > 0);
    const qint64 t0 = pts.at(0).toMap().value("t").toLongLong();
    QCOMPARE(t1 - t0, 5000LL);
}

// GPX « device-like » généré : 204 trkpt, altitude bruitée et horodatage
// 2 s. L'ancien test lisait /tmp/test-traj.gpx et était TOUJOURS ignoré
// (QSKIP) en CI — celui-ci tourne partout, sans fichier externe.
void TstProfile::profileFromRealGpx()
{
    const QDateTime start = QDateTime::fromString(
                QStringLiteral("2026-11-22T07:00:00.000Z"), Qt::ISODateWithMs);
    QString gpx = QStringLiteral(
                "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>");
    for (int i = 0; i < 204; ++i) {
        const double lat = 43.6000 + i * 0.00001;
        const double lon = 1.4400 + i * 0.00001;
        // Bruit déterministe similaire à celui du GPS réel (~±0,5 m).
        const double ele = 200.0 + (i % 17) * 0.5 + (i % 7) * 0.1;
        gpx += QStringLiteral(
                "<trkpt lat=\"%1\" lon=\"%2\"><ele>%3</ele><time>%4</time></trkpt>")
                .arg(lat, 0, 'f', 7).arg(lon, 0, 'f', 7)
                .arg(ele, 0, 'f', 1)
                .arg(start.addSecs(i * 2).toString(Qt::ISODateWithMs));
    }
    gpx += QStringLiteral("</trkseg></trk></gpx>");

    QStandardPaths::setTestModeEnabled(true);
    RouteHistory hist;
    hist.addEntry("reel", 500.0, 10.0, QVariantList(), gpx);
    const QVariantList pts = hist.profileAt(0);
    QCOMPARE(pts.size(), 204);
    double minE = 1e9, maxE = -1e9;
    int withEle = 0;
    for (const QVariant &v : pts) {
        const QVariantMap m = v.toMap();
        if (m.contains(QStringLiteral("ele"))) {
            ++withEle;
            minE = qMin(minE, m.value(QStringLiteral("ele")).toDouble());
            maxE = qMax(maxE, m.value(QStringLiteral("ele")).toDouble());
        }
    }
    QCOMPARE(withEle, 204);
    QVERIFY(minE >= 197.0 && maxE <= 209.0);
    const qint64 ta = pts.at(0).toMap().value(QStringLiteral("t")).toLongLong();
    const qint64 tb = pts.at(1).toMap().value(QStringLiteral("t")).toLongLong();
    QCOMPARE(tb - ta, 2000LL);  // horodatage extrait sur un vrai tracé bruité
}

QTEST_MAIN(TstProfile)
#include "tst_profile.moc"
