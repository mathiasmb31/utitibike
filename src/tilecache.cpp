
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
    const QString ext = (server == QLatin1String("satellite")
                         || server == QLatin1String("dark"))
            ? QStringLiteral(".jpg") : QStringLiteral(".png");
    return dir.filePath(flatName(z, x, y, ext));
}

QString tileUrl(const QString &server, int z, int x, int y)
{
    if (server == QLatin1String("cyclosm"))
        return QStringLiteral("https://a.tile-cyclosm.openstreetmap.fr/cyclosm/%1/%2/%3.png").arg(z).arg(x).arg(y);
    if (server == QLatin1String("dark"))
        // v0.99.1: CARTO dark_all renvoie désormais une image « API KEY
        // REQUIRED » avec un HTTP 200 (cache empoisonné). Esri Dark Gray
        // Canvas est gratuit sans clé — même domaine que le satellite.
        return QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/%1/%2/%3")
                .arg(z).arg(y).arg(x);
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
    purgeOldDarkTiles();
}

TileCache::~TileCache()
{
    cancelAll();
}

bool TileCache::online() const
{
    return m_online;
}

QString TileCache::tileServer() const
{
    // Appelé depuis le thread de rendu (TileMapItem::paint) : le QString est
    // partagé avec le thread principal, sa copie doit être protégée.
    QMutexLocker locker(&m_mutex);
    return m_tileServer;
}

void TileCache::setOnline(bool v)
{
    if (m_online == v)
        return;
    {
        QMutexLocker locker(&m_mutex);
        m_online = v;  // lu par tile() depuis le thread de rendu
    }
    // Hors ligne : la sonde périodique détecte le retour du réseau.
    if (v)
        m_probeTimer.stop();
    else
        m_probeTimer.start();
    qWarning() << "tilecache online=" << v;
    emit onlineChanged();
    // v0.99.1: le retour reseau doit relancer la file laisseee en attente.
    if (v)
        drain();
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
    {
        QMutexLocker locker(&m_mutex);
        m_paused = v;  // lu par tile() depuis le thread de rendu
    }
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
    bool online = false;
    bool paused = false;
    {
        QMutexLocker locker(&m_mutex);
        if (QImage *img = m_mem.object(key))
            return *img;
        if (m_pending.contains(key)) {
            // v0.99.1: la tuile est peut-être en attente dans le gros file
            // d'itinéraire — on la remonte en tête (sur le thread principal).
            QMetaObject::invokeMethod(this, [this, key]() { promoteVisible(key); },
                                      Qt::QueuedConnection);
            return QImage();
        }
        tileServer = m_tileServer;
        dir = m_dir;
        online = m_online;    // v0.99.1: lire sous mutex (ecrit par setOnline)
        paused = m_paused;
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

    if (online && !paused)
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
        // v0.99.1: les tuiles VISIBLES passent devant le gros file de
        // prechargement d'itineraire (jusqu'a 1500 tuiles pour une sortie
        // de 97 km). Sinon deplacer la carte restait blanc pendant que la
        // corridor se telechargeait.
        if (!batch) {
            m_interactiveKeys.insert(key);
            if (!m_tilesLoading) {
                m_tilesLoading = true;
                emit tilesLoadingChanged();
            }
        }
        m_queue.prepend(key);
        drain();
    }, Qt::QueuedConnection);
}

void TileCache::drain()
{
    // v0.99.1: ne pas lancer de salves de telechargement quand le reseau est
    // declare hors ligne (les cles seraient consommees sans jamais etre
    // retelechargees au retour du reseau).
    while (!m_paused && m_online && m_active.size() < MaxActive && !m_queue.isEmpty()) {
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
            const bool wasInteractive = m_interactiveKeys.remove(key) > 0;
            if (wasInteractive && m_interactiveKeys.isEmpty() && m_tilesLoading) {
                m_tilesLoading = false;
                emit tilesLoadingChanged();
            }
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

            // v0.99.1: une reponse d'ancienne generation (couche changee
            // entre-temps, requete annulee) ne doit pas piloter l'etat
            // reseau global — sinon une erreur sur une tuile abandonnee
            // peut basculer l'app en hors-ligne.
            if (current) updateOnlineState(error, ok);

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
                m_retries.remove(key);
            } else if (!ok && current) {
                // v0.99.1: une tuile en echec restait "pending" puis etait
                // oubliee — elle ne revenait que si le QML se redessinait
                // par hasard. On reessaye quelques fois (2 max) apres un
                // court delai, puis on abandonne pour ne pas saturer le
                // serveur.
                retryLater(key, generation);
            }

            if (current && m_batchKeys.remove(key)) {
                m_batchDone++;
                emitProgress();
            }
            drain();
        });
    }
}

void TileCache::promoteVisible(const QString &key)
{
    // m_queue est principal-thread only : cette méthode est toujours
    // appellée via invokeMethod() depuis le thread de rendu.
    if (m_queue.isEmpty() || m_queue.head() == key)
        return;
    QQueue<QString> others;
    bool found = false;
    while (!m_queue.isEmpty()) {
        const QString k = m_queue.dequeue();
        if (k == key) {
            found = true;
            continue;
        }
        others.enqueue(k);
    }
    if (!found)
        return;
    m_queue.enqueue(key);
    while (!others.isEmpty())
        m_queue.enqueue(others.dequeue());
}

void TileCache::retryLater(const QString &key, int generation)
{
    const int tries = m_retries.value(key, 0) + 1;
    if (tries > 2) {
        m_retries.remove(key);
        return;
    }
    m_retries.insert(key, tries);
    QTimer::singleShot(1500 * tries, this, [this, key, generation]() {
        // Un changement de couche/refresh entre temps rend ce retry obsolète.
        if (generation != m_serverGeneration)
            return;
        {
            QMutexLocker locker(&m_mutex);
            if (m_pending.contains(key))
                return;
            m_pending.insert(key);
        }
        m_queue.enqueue(key);
        drain();
    });
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
bool TileCache::enqueueKey(int z, int x, int y, bool force)
{
    const int n = 1 << z;
    if (y < 0 || y >= n)
        return false;
    const int wx = ((x % n) + n) % n;
    const QString key = keyOf(z, wx, y);
    if (force) {
        // Rafraîchissement : la tuile sur disque et en mémoire est considérée
        // comme périmée, on la supprime avant de la remettre en file.
        QFile::remove(tileFilePath(m_dir, m_tileServer, z, wx, y));
        QMutexLocker locker(&m_mutex);
        m_mem.remove(key);
        m_pending.remove(key);
        m_retries.remove(key);
    }
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
                          double lat2, double lon2, int z, bool force)
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
    // v0.99.1: plafond identique a downloadRoute — le timer de prechargement
    // de MapPage peut demander une grande zone a z17 (des milliers de tuiles).
    for (int tx = tx0; tx <= tx1 && m_batchTotal < MaxRouteTiles; ++tx)
        for (int ty = ty0; ty <= ty1 && m_batchTotal < MaxRouteTiles; ++ty)
            if (enqueueKey(z, tx, ty, force))
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

void TileCache::refreshArea(double lat1, double lon1,
                            double lat2, double lon2,
                            int zMin, int zMax)
{
    // v0.99.1: « Précharger » ignorait les tuiles déjà sur disque — il ne
    // servait donc jamais à corriger un cache périmé ou une tuile vide.
    // Ici on annule les téléchargements en cours, on supprime les fichiers
    // et on vide le cache mémoire de la zone avant de la retélécharger.
    cancelAll();
    // Invalide les retries en vol et les réponses d'avant le refresh.
    ++m_serverGeneration;
    beginBatch();
    for (int z = zMin; z <= zMax; ++z)
        enqueueBox(lat1, lon1, lat2, lon2, z, true);
    emitProgress();
    drain();
    qWarning() << "tilecache refresh area: z" << zMin << "-" << zMax
               << "," << m_batchTotal << "tuiles";
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
    // v1.0: ne pas attendre finished() pour libérer les slots — sinon un
    // abort() sans finished() laisse m_active plein et drain() bloqué.
    m_active.clear();
    m_queue.clear();
    m_batchKeys.clear();
    m_retries.clear();
    if (!m_interactiveKeys.isEmpty() || m_tilesLoading) {
        m_interactiveKeys.clear();
        m_tilesLoading = false;
        emit tilesLoadingChanged();
    }
    m_batchTotal = 0;
    m_batchDone = 0;
    // v0.99.1: les cles deja enfilees par fetch() restaient dans m_pending
    // apres abort() — leur finished() ne venant jamais, tuiles noires
    // definitives jusqu'au redemarrage.
    {
        QMutexLocker locker(&m_mutex);
        m_pending.clear();
    }
    emitProgress();
}

void TileCache::emitProgress()
{
    emit progress(m_batchDone, m_batchTotal);
}

void TileCache::purgeOldDarkTiles()
{
    // Migration v1.0: les anciennes tuiles CARTO « API KEY REQUIRED »
    // étaient enregistrées en .png dans tiles/dark. Le nouveau fournisseur
    // Esri fournit du JPEG : on supprime les .png périmés au démarrage.
    const QDir d(appDataDir() + QStringLiteral("/tiles/dark"));
    if (!d.exists())
        return;
    int removed = 0;
    for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) {
        if (fi.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0
                && QFile::remove(fi.absoluteFilePath()))
            ++removed;
    }
    if (removed > 0)
        qWarning() << "tilecache: purged" << removed << "old CARTO dark tiles";
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

