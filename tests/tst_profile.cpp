#include <QtTest>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

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
}

// Vrai GPX enregistré sur le téléphone (204 trkpt, ele 197.3–208.4)
void TstProfile::profileFromRealGpx()
{
    QFile f(QStringLiteral("/tmp/test-traj.gpx"));
    if (!f.open(QIODevice::ReadOnly)) {
        QSKIP("pas de /tmp/test-traj.gpx (copier depuis le téléphone)");
    }
    const QString gpx = QString::fromUtf8(f.readAll());
    f.close();

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
}

QTEST_MAIN(TstProfile)
#include "tst_profile.moc"
