#include "appdata.h"
#include "routerecorder.h"
#include "gpstracker.h"
#include "geo.h"

#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QXmlStreamWriter>
#include <QtMath>
#include <QDebug>

RouteRecorder::RouteRecorder(GpsTracker *gps, QObject *parent)
    : QObject(parent)
    , m_gps(gps)
{
    // Auto-sauvegarde toutes les 60 s pendant l'enregistrement : si l'app est
    // tuée (ou crash), le parcours est perdu au-delà du dernier point sauvé.
    // Le fichier "en-cours.gpx" est écrasé à chaque fois (QSaveFile atomique).
    m_autoSaveTimer.setInterval(60000);
    connect(&m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        if (m_recording)
            saveGpx(QStringLiteral("en-cours.gpx"));
    });
    connect(m_gps, &GpsTracker::positionUpdated, this, [this]() {
        if (!m_recording || !m_gps->hasFix())
            return;
        const double lat = m_gps->latitude();
        const double lon = m_gps->longitude();
        const double alt = m_gps->altitude();
        const bool altOk = m_gps->hasAltitude();

        // Ignore les sauts GPS aberrants (> 200 m en 1 s = 720 km/h)
        if (m_hasLast) {
            const double d = haversine(m_lastLat, m_lastLon, lat, lon);
            if (d > 200.0)
                return;
            m_distance += d;
            // D+ uniquement entre altitudes valides (un 0 m sans altitude
            // ne doit pas compter comme un point bas).
            if (altOk && m_hasAlt && alt > m_lastAlt)
                m_ascent += alt - m_lastAlt;
        }

        QVariantMap p;
        p.insert(QStringLiteral("lat"), lat);
        p.insert(QStringLiteral("lon"), lon);
        // N'insérer ele que si l'altitude est valide : un <ele>0.0</ele> dans
        // le GPX serait interprété comme « niveau de la mer » par Garmin/Strava.
        if (altOk)
            p.insert(QStringLiteral("ele"), alt);
        p.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        m_points.append(p);

        m_lastLat = lat;
        m_lastLon = lon;
        m_lastAlt = alt;
        m_hasAlt = altOk;
        m_hasLast = true;

        emit pointAdded(p);
        emit pointsChanged();
        emit statsChanged();
    });
}

int RouteRecorder::elapsedSec() const
{
    return m_recording ? m_elapsed.elapsed() / 1000 : 0;
}

double RouteRecorder::avgSpeedKmh() const
{
    const int secs = elapsedSec();
    if (secs < 1)
        return 0.0;
    return (m_distance / 1000.0) / (secs / 3600.0);
}

void RouteRecorder::start()
{
    if (m_recording)
        return;
    m_points.clear();
    m_distance = 0.0;
    m_ascent = 0.0;
    m_hasLast = false;
    m_hasAlt = false;
    m_startTime = QDateTime::currentDateTimeUtc();
    m_elapsed.start();
    m_recording = true;
    m_autoSaveTimer.start();
    emit recordingChanged();
    emit statsChanged();
    emit pointsChanged();
}

void RouteRecorder::stop()
{
    if (!m_recording)
        return;
    m_autoSaveTimer.stop();
    m_recording = false;
    emit recordingChanged();
    emit statsChanged();
}

void RouteRecorder::clear()
{
    if (m_points.isEmpty() && m_distance == 0.0 && m_ascent == 0.0)
        return;
    m_points.clear();
    m_distance = 0.0;
    m_ascent = 0.0;
    m_hasLast = false;
    m_hasAlt = false;
    emit pointsChanged();
    emit statsChanged();
}

QString RouteRecorder::saveGpx() const
{
    const QString name = QStringLiteral("parcours-")
                         + m_startTime.toString(QStringLiteral("yyyyMMdd-HHmmss"))
                         + QStringLiteral(".gpx");
    return saveGpx(name);
}

QString RouteRecorder::saveGpx(const QString &fileName) const
{
    if (m_points.size() < 2)
        return QString();

    const QString dir = appDataDir()
                        + QStringLiteral("/tracks");
    QDir().mkpath(dir);
    const QString name = fileName;
    const QString path = dir + QLatin1Char('/') + name;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("utitibike"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    w.writeStartElement(QStringLiteral("trk"));
    w.writeTextElement(QStringLiteral("name"), name);
    w.writeStartElement(QStringLiteral("trkseg"));
    for (const QVariant &v : m_points) {
        const QVariantMap m = v.toMap();
        w.writeStartElement(QStringLiteral("trkpt"));
        w.writeAttribute(QStringLiteral("lat"), QString::number(m.value(QStringLiteral("lat")).toDouble(), 'f', 7));
        w.writeAttribute(QStringLiteral("lon"), QString::number(m.value(QStringLiteral("lon")).toDouble(), 'f', 7));
        // <ele> uniquement si présent (altitude valide au moment du fix)
        if (m.contains(QStringLiteral("ele")))
            w.writeTextElement(QStringLiteral("ele"), QString::number(m.value(QStringLiteral("ele")).toDouble(), 'f', 1));
        w.writeTextElement(QStringLiteral("time"), m.value(QStringLiteral("time")).toString());
        w.writeEndElement(); // trkpt
    }
    w.writeEndElement(); // trkseg
    w.writeEndElement(); // trk
    w.writeEndElement(); // gpx
    w.writeEndDocument();
    if (!f.commit()) {
        qWarning() << "RouteRecorder save FAIL" << path;
        return QString();
    }
    qWarning() << "RouteRecorder saved" << path << m_points.size() << "points";
    return QUrl::fromLocalFile(path).toString();
}
