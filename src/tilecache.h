
#ifndef TILECACHE_H
#define TILECACHE_H

#include <QObject>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QDir>
#include <QHash>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

class TileCache : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY onlineChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QString tileServer READ tileServer WRITE setTileServer NOTIFY tileServerChanged)

public:
    explicit TileCache(QObject *parent = nullptr);
    ~TileCache() override;

    bool online() const;
    void setOnline(bool v);
    bool paused() const;
    void setPaused(bool v);
    QString tileServer() const { return m_tileServer; }
    void setTileServer(const QString &s);

    QImage tile(int z, int x, int y);

    Q_INVOKABLE void downloadArea(double lat1, double lon1,
                                  double lat2, double lon2,
                                  int zMin, int zMax);
    Q_INVOKABLE void cancelAll();

signals:
    void tileReady(int z, int x, int y);
    void onlineChanged();
    void pausedChanged();
    void tileServerChanged();
    void progress(int done, int total);

private:
    void fetch(int z, int x, int y, bool batch);
    void drain();
    void emitProgress();
    void purgeDiskCache();
    void updateOnlineState(int error, bool ok);
    static QString keyOf(int z, int x, int y);

    QDir m_dir;
    QCache<QString, QImage> m_mem;
    QNetworkAccessManager *m_nam = nullptr;
    QQueue<QString> m_queue;
    QSet<QString> m_pending;
    // m_mem and m_pending are also touched from the scene graph render thread
    // (TileMapItem::paint -> tile()); everything else is main-thread only.
    QMutex m_mutex;
    QSet<QString> m_batchKeys;
    QHash<QString, QNetworkReply *> m_active;
    int m_batchTotal = 0;
    int m_batchDone = 0;
    bool m_online = true;
    bool m_paused = false;
    // CARTO dark_all exige désormais une clé API (tuiles « API KEY REQUIRED »),
    // donc on garde OSM standard par défaut. Le style n'est pas persisté
    // (session uniquement) : aucun risque d'écraser un choix utilisateur.
    QString m_tileServer = QStringLiteral("osm");
    // Auto-détection réseau : 3 échecs réseau consécutifs -> hors ligne ;
    // une sonde toutes les 30 s tente de télécharger la tuile monde (z0) pour
    // détecter le retour en ligne.
    int m_consecutiveFailures = 0;
    QTimer m_probeTimer;

    static const int MaxActive = 6;
};

#endif // TILECACHE_H

