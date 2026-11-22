#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

#include "daemoncontroller.h"

// Tests du contrôleur daemon : récupération d'un en-cours.part laissé par un
// daemon crashé, départ manuel, option bip persistée.
class TstDaemonController : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void recoverOrphanPart();
    void manualStartAndBeepConf();
    void resyncWithZeroStats();
    void doneImport();
};

void TstDaemonController::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TstDaemonController::recoverOrphanPart()
{
    DaemonController dc;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString tracks = dir + QStringLiteral("/tracks");
    QDir().mkpath(tracks);
    QFile::remove(tracks + QStringLiteral("/en-cours.part"));

    // 3 points en ligne droite (~111 m par 0.001°), D+ = 10 m
    QFile f(tracks + QStringLiteral("/en-cours.part"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("{\"lat\":43.6000,\"lon\":1.4400,\"ele\":150.0,\"time\":\"2026-11-03T10:00:00.000Z\"}\n");
    f.write("{\"lat\":43.6010,\"lon\":1.4400,\"ele\":155.0,\"time\":\"2026-11-03T10:00:05.000Z\"}\n");
    f.write("{\"lat\":43.6020,\"lon\":1.4400,\"ele\":160.0,\"time\":\"2026-11-03T10:00:10.000Z\"}\n");
    f.close();

    QVERIFY(dc.recoverOrphanPart());
    QCOMPARE(dc.pointCount(), 3);
    QVERIFY(dc.distanceM() > 200.0 && dc.distanceM() < 230.0);  // ~222 m
    QCOMPARE(dc.ascentM(), 10.0);
    QVERIFY(dc.lastGpxContent().contains(QStringLiteral("<trkpt")));
    QVERIFY(dc.lastGpxContent().contains(QStringLiteral("utitibike-gpsd-recovery")));
    QVERIFY(!dc.lastGpxPath().isEmpty());
    QVERIFY(QFile::exists(dc.lastGpxPath()));
    // Le .part est consommé (supprimé) après récupération
    QVERIFY(!QFile::exists(tracks + QStringLiteral("/en-cours.part")));

    // Sans .part → rien à récupérer
    QVERIFY(!dc.recoverOrphanPart());
}

void TstDaemonController::manualStartAndBeepConf()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // État propre AVANT la construction (le ctor lit gpsd.conf) : le
    // répertoire de test persiste entre les runs.
    QFile::remove(dir + QStringLiteral("/gpsd.conf"));
    QFile::remove(dir + QStringLiteral("/manual-start.control"));

    DaemonController dc;

    // Départ manuel : écrit "lat lon" dans manual-start.control
    dc.setManualStart(43.6, 1.44);
    QFile f(dir + QStringLiteral("/manual-start.control"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(f.readAll()).simplified(),
             QStringLiteral("43.6000000 1.4400000"));
    f.close();

    // Option bip persistée dans gpsd.conf
    QVERIFY(dc.beepEnabled());  // défaut : actif
    dc.setBeepEnabled(false);
    QVERIFY(!dc.beepEnabled());
    QFile cf(dir + QStringLiteral("/gpsd.conf"));
    QVERIFY(cf.open(QIODevice::ReadOnly));
    QVERIFY(cf.readAll().contains("beep=0"));
    cf.close();
    // Persistance : un nouveau contrôleur relit la conf
    DaemonController dc2;
    QVERIFY(!dc2.beepEnabled());
}

void TstDaemonController::resyncWithZeroStats()
{
    // v0.99.1: un daemon vient de demarrer — recording.control existe mais
    // recording.json contient encore 0 point / 0 s. L'app redemarree dans
    // cette fenetre doit se resynchroniser, PAS supprimer le control file.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString control = dir + QStringLiteral("/recording.control");
    const QString stats = dir + QStringLiteral("/recording.json");
    QFile::remove(dir + QStringLiteral("/recording.done"));
    QFile::remove(control);
    QFile::remove(stats);

    QFile cf(control);
    QVERIFY(cf.open(QIODevice::WriteOnly));
    cf.write("99999 session-zero-stats\n");
    cf.close();
    QFile sf(stats);
    QVERIFY(sf.open(QIODevice::WriteOnly));
    sf.write("{\"elapsedSec\":0,\"pointCount\":0,\"distanceM\":0,\"ascentM\":0}");
    sf.close();

    DaemonController dc;
    dc.checkExistingRecording();
    QVERIFY(dc.recording());
    QVERIFY(QFile::exists(control));

    QFile::remove(control);
    QFile::remove(stats);
}

void TstDaemonController::doneImport()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString done = dir + QStringLiteral("/recording.done");
    const QString gpx = QStringLiteral(
        "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>"
        "<trkpt lat=\"43.6000\" lon=\"1.4400\"><ele>150</ele></trkpt>"
        "<trkpt lat=\"43.6010\" lon=\"1.4400\"><ele>160</ele></trkpt>"
        "</trkseg></trk></gpx>");
    QJsonObject o;
    o[QStringLiteral("recording")] = false;
    o[QStringLiteral("distanceM")] = 222.0;
    o[QStringLiteral("ascentM")] = 10.0;
    o[QStringLiteral("elapsedSec")] = 10;
    o[QStringLiteral("pointCount")] = 2;
    o[QStringLiteral("startTime")] = QStringLiteral("2026-11-22T07:00:00.000");
    o[QStringLiteral("gpxPath")] = QString();
    o[QStringLiteral("gpxContent")] = gpx;
    o[QStringLiteral("stopReason")] = QStringLiteral("user");
    QFile f(done);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();

    DaemonController dc;
    QSignalSpy spy(&dc, &DaemonController::doneReceived);
    dc.checkExistingRecording();
    QCOMPARE(spy.count(), 1);
    QVERIFY(dc.lastGpxContent().contains(QStringLiteral("<trkpt")));
    QCOMPARE(dc.pointCount(), 2);
    QVERIFY(!QFile::exists(done));  // consommé
}

QTEST_MAIN(TstDaemonController)
#include "tst_daemoncontroller.moc"
