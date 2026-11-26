#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include "tilecache.h"
#include "appdata.h"

// v1.0: premier test de TileCache (auparavant compilé dans tst_tilemapitem
// mais jamais instancié). Pas de réseau : on force setOnline(false).
class TstTileCache : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void initTestCase();
    void defaultServerAndSwitch();
    void onlineAndPausedSignals();
    void refreshAndCancelWithoutNetwork();
    void promoteVisibleKeepsQueue();
    void probeRestoresOnline();
    void refreshKeepsOldTileOnFailure();
};

void TstTileCache::init()
{
    // Chaque test qui pose une surcharge d'URL la nettoie aussi en fin de
    // slot, mais un QVERIFY prématuré pourrait l'oublier.
    qunsetenv("UTITIBIKE_TILE_URL");
}

void TstTileCache::cleanup()
{
    qunsetenv("UTITIBIKE_TILE_URL");
}

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

void TstTileCache::promoteVisibleKeepsQueue()
{
    // Régression v1.0 : promoteVisible() vidait TOUTE la file quand la clé
    // demandée n'y était plus (déjà en vol dans m_active). Conséquence :
    // tuiles jamais téléchargées + clés bloquées en m_pending et indicateur
    // « chargement » allumé indéfiniment.
    TileCache cache;
    cache.setOnline(false);   // aucune requête réseau
    // refreshArea(force) garantit que toutes les clés sont enfilées, même si
    // des tuiles de cette zone existent déjà dans le cache de test.
    cache.refreshArea(0.0, 0.0, 1.0, 1.0, 10, 10);
    const int before = cache.m_queue.size();
    QVERIFY(before >= 2);

    // Clé absente de la file (cas d'une tuile déjà en vol) : la file doit
    // rester intacte.
    cache.promoteVisible(QStringLiteral("99/99/99"));
    QCOMPARE(cache.m_queue.size(), before);

    // Clé présente : elle remonte en tête, sans perte non plus.
    const QString last = cache.m_queue.last();
    cache.promoteVisible(last);
    QCOMPARE(cache.m_queue.size(), before);
    QCOMPARE(cache.m_queue.head(), last);
}

void TstTileCache::probeRestoresOnline()
{
    // Régression v1.0 : la sonde de retour réseau passait par fetch()/drain(),
    // qui refusent de télécharger tant que m_online est faux — la sonde ne
    // partait donc jamais et l'app restait bloquée hors ligne.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    // PNG 1x1 rouge, valide pour QImage::fromData().
    const QByteArray png = QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
    QObject::connect(&server, &QTcpServer::newConnection, [&server, png]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *s = server.nextPendingConnection();
            QObject::connect(s, &QTcpSocket::readyRead, s, [s, png]() {
                s->readAll();
                const QByteArray response =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: ")
                    + QByteArray::number(png.size())
                    + "\r\nConnection: close\r\n\r\n" + png;
                s->write(response);
                s->flush();
                s->disconnectFromHost();
            });
        }
    });
    qputenv("UTITIBIKE_TILE_URL",
            QByteArray("http://127.0.0.1:") + QByteArray::number(server.serverPort())
            + "/%1/%2/%3.png");

    TileCache cache;
    cache.setOnline(false);
    QSignalSpy onlineSpy(&cache, &TileCache::onlineChanged);
    QVERIFY(!cache.online());

    // La sonde doit partir malgré m_online == false, sans allumer l'indicateur.
    cache.probeNetwork();
    QTRY_VERIFY_WITH_TIMEOUT(cache.online(), 5000);
    QCOMPARE(onlineSpy.count(), 1);
    QVERIFY(!cache.tilesLoading());
}

void TstTileCache::refreshKeepsOldTileOnFailure()
{
    // Régression : refreshArea(force) supprimait l'ancienne tuile du disque
    // et du cache mémoire AVANT le téléchargement. Si le serveur timeout
    // (observé sur CyclOSM le 25/11), la carte restait grise définitivement.
    const QByteArray png = QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
    const QString dir = appDataDir() + QStringLiteral("/tiles/osm");
    QVERIFY(QDir().mkpath(dir));
    const QString path = dir + QStringLiteral("/10-512-512.png");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(png), static_cast<qint64>(png.size()));
    }

    TileCache cache;
    cache.setOnline(false);   // aucun téléchargement : cas du serveur en échec
    cache.refreshArea(0.0, 0.0, 0.0, 0.0, 10, 10);

    // L'ancienne tuile reste affichée et le fichier n'est pas supprimé.
    QVERIFY(QFile::exists(path));
    QVERIFY(!cache.tile(10, 512, 512).isNull());
    // ... mais le re-téléchargement est bien prévu malgré la tuile sur disque.
    QVERIFY(cache.m_queue.contains(QStringLiteral("10/512/512")));

    QFile::remove(path);
}

QTEST_GUILESS_MAIN(TstTileCache)
#include "tst_tilecache.moc"
