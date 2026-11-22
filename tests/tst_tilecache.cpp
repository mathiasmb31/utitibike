#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include "tilecache.h"

// v1.0: premier test de TileCache (auparavant compilé dans tst_tilemapitem
// mais jamais instancié). Pas de réseau : on force setOnline(false).
class TstTileCache : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void defaultServerAndSwitch();
    void onlineAndPausedSignals();
    void refreshAndCancelWithoutNetwork();
};

void TstTileCache::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TstTileCache::defaultServerAndSwitch()
{
    TileCache cache;
    QCOMPARE(cache.tileServer(), QStringLiteral("osm"));
    QSignalSpy spy(&cache, &TileCache::tileServerChanged);
    cache.setTileServer(QStringLiteral("dark"));
    QCOMPARE(cache.tileServer(), QStringLiteral("dark"));
    QCOMPARE(spy.count(), 1);
}

void TstTileCache::onlineAndPausedSignals()
{
    TileCache cache;
    QSignalSpy onlineSpy(&cache, &TileCache::onlineChanged);
    QSignalSpy pausedSpy(&cache, &TileCache::pausedChanged);
    cache.setOnline(false);
    cache.setPaused(true);
    QCOMPARE(onlineSpy.count(), 1);
    QCOMPARE(pausedSpy.count(), 1);
    QVERIFY(!cache.online());
    QVERIFY(cache.paused());
}

void TstTileCache::refreshAndCancelWithoutNetwork()
{
    TileCache cache;
    cache.setOnline(false);
    QSignalSpy progressSpy(&cache, &TileCache::progress);
    // Bbox autour de Toulouse : la file doit être remplie sans télécharger.
    cache.refreshArea(43.60, 1.44, 43.61, 1.45, 10, 12);
    QVERIFY(progressSpy.count() >= 1);
    // cancelAll() ne doit pas bloquer ni laisser l'indicateur allumé.
    cache.cancelAll();
    QVERIFY(!cache.tilesLoading());
}

QTEST_GUILESS_MAIN(TstTileCache)
#include "tst_tilecache.moc"
