#include "gpsdaemon.h"
#include "geo.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QtMath>
#include <csignal>
#include <unistd.h>
#include <sys/prctl.h>

// Pour les signaux UNIX (SIGTERM, SIGINT) — le daemon s'arrête proprement.
static GpsDaemon *g_daemon = nullptr;
static void signalHandler(int) {
    if (g_daemon)
        QMetaObject::invokeMethod(g_daemon, "cleanup", Qt::QueuedConnection);
}

GpsDaemon::GpsDaemon(QObject *parent)
    : QObject(parent)
{
    // Même applicationName que l'app principale pour partager appDataDir.
    m_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_controlFile = m_appDataDir + QStringLiteral("/recording.control");
    m_statsFile   = m_appDataDir + QStringLiteral("/recording.json");
    m_doneFile    = m_appDataDir + QStringLiteral("/recording.done");
    m_tracksDir   = m_appDataDir + QStringLiteral("/tracks");

    QDir().mkpath(m_tracksDir);

    // Beep : fichier installé à côté de l'exécutable dans le click.
    m_beep.setSource(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/beep.wav")));
    m_beep.setVolume(0.5f);

    // GPS
    m_gps = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_gps) {
        qWarning() << "[gpsd] no position source — abort";
        QCoreApplication::quit();
        return;
    }
    // 1 fix toutes les 5 s : à 20 km/h = un point tous les 28 m, très
    // suffisant pour du vélo. Intervalle plus espacé = moins de conso
    // batterie (le GPS est le 2e poste de conso après l'écran).
    m_gps->setUpdateInterval(5000);
    connect(m_gps, &QGeoPositionInfoSource::positionUpdated,
            this, &GpsDaemon::onPositionUpdated);

    // Timers
    m_autoSaveTimer.setInterval(60000);   // 60 s
    m_statsTimer.setInterval(2000);       // 2 s
    m_beepTimer.setInterval(10000);       // 10 s
    m_controlTimer.setInterval(2000);     // 2 s

    connect(&m_autoSaveTimer, &QTimer::timeout, this, &GpsDaemon::onAutoSave);
    connect(&m_statsTimer, &QTimer::timeout, this, &GpsDaemon::onStatsTick);
    connect(&m_beepTimer, &QTimer::timeout, this, &GpsDaemon::onBeepTick);
    connect(&m_controlTimer, &QTimer::timeout, this, &GpsDaemon::onControlTick);
}

GpsDaemon::~GpsDaemon()
{
    cleanup();
}

void GpsDaemon::run()
{
    g_daemon = this;
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);

    // Sécurité : le noyau envoie SIGTERM à ce processus quand le parent
    // (l'app QML) meurt — même en cas de crash ou kill -9 de l'app.
    // PR_SET_PDEATHSIG ne fonctionne que si le parent est encore vivant
    // au moment de l'appel, d'où la double vérification.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1) {
        // Parent déjà mort (lancé via startDetached) — on vérifie le PID
        // dans le fichier de contrôle à la place.
        qWarning() << "[gpsd] parent already dead, relying on PID check";
    }

    m_gps->startUpdates();
    m_controlTimer.start();
    qWarning() << "[gpsd] daemon started, appDataDir=" << m_appDataDir
               << "ppid=" << getppid();
}

void GpsDaemon::onPositionUpdated(const QGeoPositionInfo &info)
{
    if (!m_recording || !info.isValid())
        return;

    QGeoCoordinate coord = info.coordinate();
    if (!coord.isValid())
        return;

    const double lat = coord.latitude();
    const double lon = coord.longitude();
    const double alt = coord.altitude();
    const bool altOk = !qIsNaN(alt);

    // Ignore les sauts GPS aberrants (> 200 m en 2 s = 360 km/h)
    if (m_hasLast) {
        const double d = haversine(m_lastLat, m_lastLon, lat, lon);
        if (d > 200.0)
            return;
        m_distance += d;
        if (altOk && m_hasAlt && alt > m_lastAlt)
            m_ascent += alt - m_lastAlt;
    }

    QVariantMap p;
    p.insert(QStringLiteral("lat"), lat);
    p.insert(QStringLiteral("lon"), lon);
    if (altOk)
        p.insert(QStringLiteral("ele"), alt);
    p.insert(QStringLiteral("time"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_points.append(p);

    m_lastLat = lat;
    m_lastLon = lon;
    m_lastAlt = alt;
    m_hasAlt = altOk;
    m_hasLast = true;
}

void GpsDaemon::onAutoSave()
{
    if (m_recording)
        saveGpx(QStringLiteral("en-cours.gpx"));
}

void GpsDaemon::onStatsTick()
{
    if (m_recording)
        writeStatsJson();
}

void GpsDaemon::onBeepTick()
{
    if (m_recording)
        m_beep.play();
}

void GpsDaemon::onControlTick()
{
    const bool controlExists = QFile::exists(m_controlFile);

    if (controlExists && !m_recording) {
        // Démarrage demandé par l'app — lit le PID parent depuis le fichier
        QFile cf(m_controlFile);
        if (cf.open(QIODevice::ReadOnly)) {
            m_parentPid = cf.readAll().trimmed().toInt();
            cf.close();
        }
        qWarning() << "[gpsd] parent PID from control file:" << m_parentPid;

        m_recording = true;
        m_points.clear();
        m_distance = 0.0;
        m_ascent = 0.0;
        m_hasLast = false;
        m_hasAlt = false;
        m_startTime = QDateTime::currentDateTimeUtc();
        m_elapsed.start();
        m_autoSaveTimer.start();
        m_statsTimer.start();
        m_beepTimer.start();
        writeStatsJson();
        qWarning() << "[gpsd] recording STARTED";
    } else if (m_recording) {
        // Vérifie si l'arrêt a été demandé (fichier supprimé)
        if (!controlExists) {
            qWarning() << "[gpsd] recording STOPPED by control file removal";
            cleanup();
            return;
        }
        // Sécurité secours : vérifie que l'app (parent) est encore vivante
        if (m_parentPid > 0 && kill(m_parentPid, 0) != 0) {
            qWarning() << "[gpsd] parent app dead (pid" << m_parentPid
                       << ") — stopping recording";
            cleanup();
            return;
        }
    }
}

void GpsDaemon::writeStatsJson()
{
    const int secs = m_elapsed.elapsed() / 1000;
    const double speedKmh = secs > 0 ? (m_distance / 1000.0) / (secs / 3600.0) : 0.0;

    QJsonObject obj;
    obj[QStringLiteral("recording")] = true;
    obj[QStringLiteral("distanceM")] = m_distance;
    obj[QStringLiteral("ascentM")] = m_ascent;
    obj[QStringLiteral("elapsedSec")] = secs;
    obj[QStringLiteral("avgSpeedKmh")] = speedKmh;
    obj[QStringLiteral("pointCount")] = m_points.size();
    obj[QStringLiteral("startTime")] = m_startTime.toString(Qt::ISODateWithMs);

    QSaveFile f(m_statsFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void GpsDaemon::writeDoneJson(const QString &gpxPath)
{
    QJsonObject obj;
    obj[QStringLiteral("recording")] = false;
    obj[QStringLiteral("distanceM")] = m_distance;
    obj[QStringLiteral("ascentM")] = m_ascent;
    obj[QStringLiteral("elapsedSec")] = m_elapsed.elapsed() / 1000;
    obj[QStringLiteral("pointCount")] = m_points.size();
    obj[QStringLiteral("startTime")] = m_startTime.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("gpxPath")] = gpxPath;
    obj[QStringLiteral("gpxContent")] = generateGpx();

    QSaveFile f(m_doneFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString GpsDaemon::generateGpx() const
{
    if (m_points.size() < 2)
        return QString();

    QString gpx;
    QXmlStreamWriter w(&gpx);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("utitibike-gpsd"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    w.writeStartElement(QStringLiteral("trk"));
    w.writeTextElement(QStringLiteral("name"),
                       m_startTime.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    w.writeStartElement(QStringLiteral("trkseg"));
    for (const QVariant &v : m_points) {
        const QVariantMap m = v.toMap();
        w.writeStartElement(QStringLiteral("trkpt"));
        w.writeAttribute(QStringLiteral("lat"),
                         QString::number(m.value(QStringLiteral("lat")).toDouble(), 'f', 7));
        w.writeAttribute(QStringLiteral("lon"),
                         QString::number(m.value(QStringLiteral("lon")).toDouble(), 'f', 7));
        if (m.contains(QStringLiteral("ele")))
            w.writeTextElement(QStringLiteral("ele"),
                               QString::number(m.value(QStringLiteral("ele")).toDouble(), 'f', 1));
        w.writeTextElement(QStringLiteral("time"),
                           m.value(QStringLiteral("time")).toString());
        w.writeEndElement(); // trkpt
    }
    w.writeEndElement(); // trkseg
    w.writeEndElement(); // trk
    w.writeEndElement(); // gpx
    w.writeEndDocument();
    return gpx;
}

QString GpsDaemon::saveGpx(const QString &fileName) const
{
    const QString gpx = generateGpx();
    if (gpx.isEmpty())
        return QString();

    const QString path = m_tracksDir + QLatin1Char('/') + fileName;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(gpx.toUtf8());
    if (!f.commit()) {
        qWarning() << "[gpsd] save FAIL" << path;
        return QString();
    }
    return path;
}

void GpsDaemon::cleanup()
{
    if (!m_recording) {
        // Arrêt demandé avant démarrage — quitte directement
        QCoreApplication::quit();
        return;
    }

    m_recording = false;
    m_autoSaveTimer.stop();
    m_statsTimer.stop();
    m_beepTimer.stop();
    m_controlTimer.stop();
    m_gps->stopUpdates();

    // Sauvegarde finale
    const QString name = QStringLiteral("parcours-")
                         + m_startTime.toString(QStringLiteral("yyyyMMdd-HHmmss"))
                         + QStringLiteral(".gpx");
    const QString gpxPath = saveGpx(name);
    writeDoneJson(gpxPath);

    // Nettoie les fichiers de contrôle
    QFile::remove(m_controlFile);
    QFile::remove(m_statsFile);

    qWarning() << "[gpsd] saved" << gpxPath << m_points.size() << "points, exiting";
    QCoreApplication::quit();
}
