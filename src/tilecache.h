
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
    // v0.99.1: vrai pendant le chargement des tuiles VISIBLES (pas le
    // préchargement d'itinéraire), pour l'indicateur stylisé de la carte.
    Q_PROPERTY(bool tilesLoading READ tilesLoading NOTIFY tilesLoadingChanged)

public:
    explicit TileCache(QObject *parent = nullptr);
    ~TileCache() override;

    bool online() const;
    void setOnline(bool v);
    bool paused() const;
    void setPaused(bool v);
    QString tileServer() const { return m_tileServer; }
    void setTileServer(const QString &s);
    bool tilesLoading() const { return m_tilesLoading; }

    QImage tile(int z, int x, int y);

    Q_INVOKABLE void downloadArea(double lat1, double lon1,
                                  double lat2, double lon2,
                                  int zMin, int zMax);
    // v0.99.1: force le re-telechargement des tuiles de la zone (supprime les
    // fichiers disque et vide le cache memoire avant de les remettre en file).
    Q_INVOKABLE void refreshArea(double lat1, double lon1,
                                 double lat2, double lon2,
                                 int zMin, int zMax);
    // Précharge un corridor de ±widthM autour d'une polyligne (itinéraire
    // calculé) pour la navigation hors ligne. Le nombre de tuiles est plafonné
    // (MaxRouteTiles) : si le corridor dépasse le plafond à zMax, on baisse
    // zMax jusqu'à rentrer dans le plafond.
    Q_INVOKABLE void downloadRoute(const QVariantList &points, double widthM,
                                   int zMin, int zMax);
    Q_INVOKABLE void cancelAll();

signals:
    void tileReady(int z, int x, int y);
    void onlineChanged();
    void pausedChanged();
    void tileServerChanged();
    void tilesLoadingChanged();
    void progress(int done, int total);

private:
    void fetch(int z, int x, int y, bool batch);
    void drain();
    void retryLater(const QString &key);
    // Ramène une tuile déjà en attente (file de préchargement) en tête.
    void promoteVisible(const QString &key);
    void emitProgress();
    void purgeDiskCache();
    void updateOnlineState(int error, bool ok);
    static QString keyOf(int z, int x, int y);
    // Enfile les tuiles d'une boîte lat/lon au zoom z (dédupliquées, tuiles
    // déjà sur disque comptées dans m_batchDone). Retourne le nombre de tuiles
    // enfilées pour cette boîte.
    int enqueueBox(double lat1, double lon1, double lat2, double lon2, int z,
                   bool force = false);
    bool enqueueKey(int z, int x, int y, bool force = false);
    void beginBatch();

    QDir m_dir;
    QCache<QString, QImage> m_mem;
    QNetworkAccessManager *m_nam = nullptr;
    QQueue<QString> m_queue;
    QSet<QString> m_pending;
    // m_mem and m_pending are also touched from the scene graph render thread
    // (TileMapItem::paint -> tile()); everything else is main-thread only.
    QMutex m_mutex;
    QSet<QString> m_batchKeys;
    // Tuiles demandées par le rendu (fetch batch=false) — pour l'indicateur.
    QSet<QString> m_interactiveKeys;
    bool m_tilesLoading = false;
    QHash<QString, QNetworkReply *> m_active;
    // v0.99.1: nombre de tentatives deja faites pour une tuile en echec
    // (2 retries max, pour ne pas boucler sur un serveur en panne).
    QHash<QString, int> m_retries;
    int m_batchTotal = 0;
    int m_batchDone = 0;
    bool m_online = true;
    bool m_paused = false;
    // Incrémenté à chaque setTileServer : les réponses réseau en vol sont
    // taguées avec la génération de leur requête et ignorées si la couche a
    // changé entre-temps (sinon une tuile de l'ancienne couche serait écrite
    // dans le répertoire du nouveau serveur).
    int m_serverGeneration = 0;
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
    // Plafond de tuiles pour un préchargement d'itinéraire (corridor)
    static const int MaxRouteTiles = 2000;
};

#endif // TILECACHE_H

