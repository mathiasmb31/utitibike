
#include "tilecache.h"
#include "appdata.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSaveFile>
#include <QtMath>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QFileInfoList>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>

namespace {

double lonToWorldX(double lon, double world)
{
    return (lon + 180.0) / 360.0 * world;
}

double latToWorldY(double lat, double world)
{
    const double r = qDegreesToRadians(lat);
    const double y = (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / M_PI) / 2.0;
    return y * world;
}

QString flatName(int z, int x, int y, const QString &ext)
{
    return QStringLiteral("%1-%2-%3%4").arg(z).arg(x).arg(y).arg(ext);
}

// Nom de fichier disque d'une tuile. Le serveur satellite (Esri) renvoie du
// JPEG, les autres serveurs du PNG.
QString tileFilePath(const QDir &dir, const QString &server, int z, int x, int y)
{
    const QString ext = (server == QLatin1String("satellite"))
            ? QStringLiteral(".jpg") : QStringLiteral(".png");
    return dir.filePath(flatName(z, x, y, ext));
}

QString tileUrl(const QString &server, int z, int x, int y)
{
    if (server == QLatin1String("cyclosm"))
        return QStringLiteral("https://a.tile-cyclosm.openstreetmap.fr/cyclosm/%1/%2/%3.png").arg(z).arg(x).arg(y);
    if (server == QLatin1String("dark"))
        return QStringLiteral("https://a.basemaps.cartocdn.com/dark_all/%1/%2/%3.png").arg(z).arg(x).arg(y);
    if (server == QLatin1String("satellite"))
        return QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3").arg(z).arg(y).arg(x);
    // Défaut : OSM standard
    return QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y);
}

} // namespace

TileCache::TileCache(QObject *parent)
    : QObject(parent)
    , m_mem(100)   // ~100 tuiles de 256x256 (~25 Mo) : assez pour la vue courante
{
    const QString base = appDataDir();
    m_dir = QDir(base + QStringLiteral("/tiles/") + m_tileServer);
    m_dir.mkpath(QStringLiteral("."));
    qWarning() << "tilecache base" << base << "dir" << m_dir.path();
    m_nam = new QNetworkAccessManager(this);
    m_probeTimer.setInterval(30000);
    connect(&m_probeTimer, &QTimer::timeout, this, [this]() {
        if (!m_online)
            fetch(0, 0, 0, false);   // tuile monde : sonde de retour en ligne
    });
    purgeDiskCache();
}

TileCache::~TileCache()
{
    cancelAll();
}

bool TileCache::online() const
{
    return m_online;
}

void TileCache::setOnline(bool v)
{
    if (m_online == v)
        return;
    m_online = v;
    // Hors ligne : la sonde périodique détecte le retour du réseau.
    if (v)
        m_probeTimer.stop();
    else
        m_probeTimer.start();
    qWarning() << "tilecache online=" << v;
    emit onlineChanged();
}

void TileCache::updateOnlineState(int error, bool ok)
{
    if (ok) {
        m_consecutiveFailures = 0;
        if (!m_online)
            setOnline(true);
        return;
    }
    // Seules les vraies pannes réseau comptent (pas les 404/403 HTTP qui
    // signifient « serveur vivant, tuile refusée »).
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::SslHandshakeFailedError:
        ++m_consecutiveFailures;
        if (m_consecutiveFailures >= 3 && m_online)
            setOnline(false);
        break;
    default:
        break;
    }
}

bool TileCache::paused() const
{
    return m_paused;
}

void TileCache::setPaused(bool v)
{
    if (m_paused == v)
        return;
    m_paused = v;
    qWarning() << "tilecache paused=" << v;
    emit pausedChanged();
    // En arrière-plan l'appli ne télécharge plus rien (batterie) ; au retour
    // au premier plan, la file en attente repart.
    if (!v)
        drain();
}

void TileCache::setTileServer(const QString &s)
{
    if (m_tileServer == s)
        return;
    // Chaque serveur a son propre sous-répertoire pour ne pas mélanger les tuiles.
    // Le mkpath (I/O disque) est fait hors mutex ; seules les assignations
    // de membres sont protégées (tile() les lit depuis le thread de rendu).
    const QString base = appDataDir();
    QDir newDir(base + QStringLiteral("/tiles/") + s);
    newDir.mkpath(QStringLiteral("."));
    // Annule les téléchargements en vol de l'ancienne couche : leurs tuiles ne
    // doivent ni être écrites dans le répertoire du nouveau serveur, ni
    // occuper m_pending (ce qui empêcherait le re-fetch des tuiles de la
    // nouvelle couche).
    cancelAll();
    {
        QMutexLocker locker(&m_mutex);
        m_tileServer = s;
        m_dir = newDir;
        // Les tuiles en cache mémoire et en attente appartiennent à l'ancien
        // serveur.
        m_mem.clear();
        m_pending.clear();
        ++m_serverGeneration;
    }
    emit tileServerChanged();
}

QString TileCache::keyOf(int z, int x, int y)
{
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
}

QImage TileCache::tile(int z, int x, int y)
{
    // Called from the scene graph render thread (TileMapItem::paint): m_mem,
    // m_pending, m_tileServer and m_dir are shared with the main thread,
    // so they must be guarded.
    const QString key = keyOf(z, x, y);
    QString tileServer;
    QDir dir;
    {
        QMutexLocker locker(&m_mutex);
        if (QImage *img = m_mem.object(key))
            return *img;
        if (m_pending.contains(key))
            return QImage();
        tileServer = m_tileServer;
        dir = m_dir;
    }

    const QString path = tileFilePath(dir, tileServer, z, x, y);
    if (QFile::exists(path)) {
        QImage img(path);
        if (!img.isNull()) {
            QMutexLocker locker(&m_mutex);
            m_mem.insert(key, new QImage(img));
            return img;
        }
    }

    if (m_online && !m_paused)
        fetch(z, x, y, false);
    return QImage();
}

void TileCache::fetch(int z, int x, int y, bool batch)
{
    // Reached from the render thread via tile(); the QNetworkAccessManager and
    // its replies live in the main thread, so marshal the work there.
    QMetaObject::invokeMethod(this, [this, z, x, y, batch]() {
        const QString key = keyOf(z, x, y);
        {
            QMutexLocker locker(&m_mutex);
            if (m_pending.contains(key))
                return;
            m_pending.insert(key);
            if (batch)
                m_batchKeys.insert(key);
        }
        m_queue.enqueue(key);
        drain();
    }, Qt::QueuedConnection);
}

void TileCache::drain()
{
    while (!m_paused && m_active.size() < MaxActive && !m_queue.isEmpty()) {
        const QString key = m_queue.dequeue();
        const QStringList parts = key.split(QLatin1Char('/'));
        if (parts.size() != 3)
            continue;
        const int z = parts.at(0).toInt();
        const int x = parts.at(1).toInt();
        const int y = parts.at(2).toInt();

        // Taguer la requête avec la couche et la génération courantes : si
        // l'utilisateur change de couche pendant le téléchargement, la réponse
        // est ignorée (pas d'écriture disque dans le mauvais répertoire).
        const QString server = m_tileServer;
        const int generation = m_serverGeneration;
        QNetworkRequest req{ QUrl{ tileUrl(server, z, x, y) } };
        // OSM redirige vers des CDN : suivre les redirections (le mode manuel
        // par défaut les bloquerait).
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("utitibike/" UTITIBIKE_VERSION " (Ubuntu Touch; tiles OpenStreetMap)"));
        QNetworkReply *reply = m_nam->get(req);
        // Timeout : une requête tuile bloquée ne doit pas occuper un des 6
        // slots actifs indéfiniment.
        QTimer *timeout = new QTimer(reply);
        timeout->setSingleShot(true);
        timeout->setInterval(15000);
        connect(timeout, &QTimer::timeout, reply, [reply]() {
            qWarning() << "tilecache timeout, abort" << reply->url().toString();
            reply->abort();
        });
        timeout->start();
        m_active.insert(key, reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, key, server, generation]() {
            m_active.remove(key);
            // Génération obsolète (changement de couche entre-temps) : la
            // tuile appartient à l'ancienne couche, on l'ignore totalement.
            const bool current = (generation == m_serverGeneration);
            if (current) {
                QMutexLocker locker(&m_mutex);
                m_pending.remove(key);
            }

            const QByteArray data = reply->readAll();
            // Valider le contenu réel (PNG ou JPEG selon le serveur) plutôt
            // qu'une signature fixe : Esri renvoie du JPEG en mode satellite.
            const QImage img = QImage::fromData(data);
            const int error = static_cast<int>(reply->error());
            const bool ok = reply->error() == QNetworkReply::NoError
                           && !img.isNull();
            reply->deleteLater();

            updateOnlineState(error, ok);

            if (ok && current) {
                const QStringList parts = key.split(QLatin1Char('/'));
                const int z = parts.at(0).toInt();
                const int x = parts.at(1).toInt();
                const int y = parts.at(2).toInt();
                QSaveFile f(tileFilePath(m_dir, m_tileServer, z, x, y));
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.commit();
                } else {
                    qWarning() << "tilecache save FAILED" << f.fileName() << f.errorString();
                }
                {
                    QMutexLocker locker(&m_mutex);
                    m_mem.insert(key, new QImage(img));
                }
                emit tileReady(z, x, y);
            }

            if (current && m_batchKeys.remove(key)) {
                m_batchDone++;
                emitProgress();
            }
            drain();
        });
    }
}

void TileCache::beginBatch()
{
    m_batchDone = 0;
    m_batchTotal = 0;
    m_batchKeys.clear();
}

// Enfile une tuile (dédupliquée ; les tuiles déjà sur disque sont comptées
// dans m_batchDone/m_batchTotal pour une progression cohérente).
// Retourne true si la tuile a été enfilée pour téléchargement.
bool TileCache::enqueueKey(int z, int x, int y)
{
    const int n = 1 << z;
    if (y < 0 || y >= n)
        return false;
    const int wx = ((x % n) + n) % n;
    const QString key = keyOf(z, wx, y);
    {
        QMutexLocker locker(&m_mutex);
        if (m_pending.contains(key))
            return false;
    }
    if (QFile::exists(tileFilePath(m_dir, m_tileServer, z, wx, y))) {
        // Tuile déjà sur disque : compte dans le total ET le fait
        // (progression cohérente, ex. 42/42 si tout est en cache).
        m_batchDone++;
        m_batchTotal++;
        return false;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_pending.insert(key);
    }
    m_batchKeys.insert(key);
    m_batchTotal++;
    m_queue.enqueue(key);
    return true;
}

int TileCache::enqueueBox(double lat1, double lon1,
                          double lat2, double lon2, int z)
{
    int added = 0;
    const double world = 256.0 * std::pow(2.0, z);
    const double xa = lonToWorldX(lon1, world);
    const double xb = lonToWorldX(lon2, world);
    const double ya = latToWorldY(lat1, world);
    const double yb = latToWorldY(lat2, world);
    const int tx0 = qFloor(std::min(xa, xb) / 256.0);
    const int tx1 = qFloor(std::max(xa, xb) / 256.0);
    const int ty0 = qFloor(std::min(ya, yb) / 256.0);
    const int ty1 = qFloor(std::max(ya, yb) / 256.0);
    for (int tx = tx0; tx <= tx1; ++tx)
        for (int ty = ty0; ty <= ty1; ++ty)
            if (enqueueKey(z, tx, ty))
                ++added;
    return added;
}

void TileCache::downloadArea(double lat1, double lon1,
                             double lat2, double lon2,
                             int zMin, int zMax)
{
    beginBatch();
    for (int z = zMin; z <= zMax; ++z)
        enqueueBox(lat1, lon1, lat2, lon2, z);
    emitProgress();
    drain();
}

void TileCache::downloadRoute(const QVariantList &points, double widthM,
                              int zMin, int zMax)
{
    if (points.size() < 2 || widthM <= 0.0)
        return;
    zMin = qBound(3, zMin, 17);
    zMax = qBound(zMin, zMax, 17);
    // Échantillonnage du tracé : au plus ~400 pastilles de rayon widthM
    const int step = qMax(1, static_cast<int>(points.size()) / 400);

    struct Box { double lat1, lon1, lat2, lon2; };
    QList<Box> boxes;
    for (int i = 0; i < points.size(); i += step) {
        const QVariantMap p = points[i].toMap();
        const double lat = p.value(QStringLiteral("lat")).toDouble();
        const double lon = p.value(QStringLiteral("lon")).toDouble();
        const double dLat = widthM / 111320.0;
        const double dLon = widthM / (111320.0 * qMax(0.01, std::cos(qDegreesToRadians(lat))));
        Box b = { lat - dLat, lon - dLon, lat + dLat, lon + dLon };
        boxes.append(b);
    }
    // Le dernier point du tracé (si le pas ne tombe pas dessus)
    if ((points.size() - 1) % step != 0) {
        const QVariantMap p = points.last().toMap();
        const double lat = p.value(QStringLiteral("lat")).toDouble();
        const double lon = p.value(QStringLiteral("lon")).toDouble();
        const double dLat = widthM / 111320.0;
        const double dLon = widthM / (111320.0 * qMax(0.01, std::cos(qDegreesToRadians(lat))));
        Box b = { lat - dLat, lon - dLon, lat + dLat, lon + dLon };
        boxes.append(b);
    }

    // Compte les tuiles du corridor pour choisir le zMax sous le plafond
    auto boxKeys = [](int z, const Box &b, QSet<QString> &out) {
        const double world = 256.0 * std::pow(2.0, z);
        const double xa = lonToWorldX(b.lon1, world), xb = lonToWorldX(b.lon2, world);
        const double ya = latToWorldY(b.lat1, world), yb = latToWorldY(b.lat2, world);
        const int tx0 = qFloor(std::min(xa, xb) / 256.0), tx1 = qFloor(std::max(xa, xb) / 256.0);
        const int ty0 = qFloor(std::min(ya, yb) / 256.0), ty1 = qFloor(std::max(ya, yb) / 256.0);
        const int n = 1 << z;
        for (int tx = tx0; tx <= tx1; ++tx)
            for (int ty = ty0; ty <= ty1; ++ty) {
                if (ty < 0 || ty >= n)
                    continue;
                out.insert(keyOf(z, ((tx % n) + n) % n, ty));
            }
    };
    int zHi = zMax;
    while (zHi > zMin) {
        QSet<QString> keys;
        for (int z = zMin; z <= zHi; ++z)
            for (const Box &b : boxes)
                boxKeys(z, b, keys);
        if (keys.size() <= MaxRouteTiles)
            break;
        --zHi;
    }

    beginBatch();
    for (int z = zMin; z <= zHi; ++z)
        for (const Box &b : boxes)
            enqueueBox(b.lat1, b.lon1, b.lat2, b.lon2, z);
    emitProgress();
    drain();
    qWarning() << "tilecache route corridor:" << boxes.size() << "boxes, z"
               << zMin << "-" << zHi << "," << m_batchTotal << "tuiles";
}

void TileCache::cancelAll()
{
    const QStringList keys = m_active.keys();
    for (const QString &key : keys)
        m_active.value(key)->abort();
    m_queue.clear();
    m_batchKeys.clear();
    m_batchTotal = 0;
    m_batchDone = 0;
    emitProgress();
}

void TileCache::emitProgress()
{
    emit progress(m_batchDone, m_batchTotal);
}

void TileCache::purgeDiskCache()
{
    // Quota global du cache disque (tous serveurs confondus) : 200 Mo.
    // Au-delà, les tuiles les plus anciennes (date de modification) sont
    // supprimées jusqu'à 150 Mo — LRU approximé, exécuté au démarrage.
    const qint64 kMaxBytes = 200LL * 1024 * 1024;
    const qint64 kTargetBytes = 150LL * 1024 * 1024;
    const QString base = appDataDir() + QStringLiteral("/tiles");
    const QDir root(base);
    const QStringList servers = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    struct Entry { QString path; qint64 size; QDateTime time; };
    QList<Entry> all;
    qint64 total = 0;
    for (const QString &srv : servers) {
        const QDir d(base + QLatin1Char('/') + srv);
        const QFileInfoList files = d.entryInfoList(QDir::Files);
        for (const QFileInfo &fi : files) {
            all.append({ fi.absoluteFilePath(), fi.size(), fi.lastModified() });
            total += fi.size();
        }
    }
    if (total <= kMaxBytes)
        return;
    std::sort(all.begin(), all.end(),
              [](const Entry &a, const Entry &b) { return a.time < b.time; });
    for (const Entry &e : all) {
        if (total <= kTargetBytes)
            break;
        if (QFile::remove(e.path))
            total -= e.size;
    }
    qWarning() << "tilecache purge disque: reste" << total / 1024 / 1024 << "Mo";
}

