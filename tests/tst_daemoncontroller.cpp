#include <QtTest>
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

QTEST_MAIN(TstDaemonController)
#include "tst_daemoncontroller.moc"
