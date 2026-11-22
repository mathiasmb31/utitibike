#include "gpstracker.h"
#include <QDebug>
#include <QDateTime>
#include <cmath>

GpsTracker::GpsTracker(QObject *parent)
    : QObject(parent)
{
    // La source GPS n'est PAS créée ici : QGeoPositionInfoSource::createDefaultSource
    // peut bloquer le thread principal au démarrage (écran blanc sur UT).
    // Elle est créée lazily au premier start().
    m_watchdogTimer.setInterval(60000);
    connect(&m_watchdogTimer, &QTimer::timeout, this, [this]() {
        if (!m_active || !m_source)
            return;
        const qint64 silence = QDateTime::currentMSecsSinceEpoch() - m_lastUpdateMs;
        if (silence < 60000)
            return;
        qWarning() << "GpsTracker: no position for" << silence
                   << "ms — restarting updates";
        m_source->stopUpdates();
        m_source->startUpdates();
        m_lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        emit statusChanged();
    });
}

QString GpsTracker::statusText() const
{
    if (!m_source)
        return tr("GPS inactif");
    if (!m_active)
        return tr("GPS arrêté");
    if (!m_hasFix && m_hasApprox)
        return tr("Position approximative…");
    if (!m_hasFix)
        return tr("Recherche du signal GPS…");
    return tr("GPS actif");
}

bool GpsTracker::ensureSource()
{
    if (m_source)
        return true;
    // Création lazy de la source GPS (peut prendre du temps)
    {
        m_source = QGeoPositionInfoSource::createDefaultSource(this);
        if (!m_source) {
            qWarning() << "GpsTracker: no position source available";
            emit statusChanged();
            return false;
        }
        // v1.0: diagnostic — quelles méthodes le plugin UT expose-t-il ?
        // (les valeurs utiles : Satellite=0xff, NonSatellite=0xffffff00,
        //  All=0xffffffff — 0 = aucune source utilisable).
        qWarning() << "GpsTracker: source" << m_source->sourceName()
                   << "supported methods"
                   << static_cast<int>(m_source->supportedPositioningMethods());
        m_source->setUpdateInterval(1000);
        // v1.0: un seul mode utile sur UT (lomiri : gps + remote).
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
        // Position de secours : dernière position connue en cache (lue sans
        // attendre le serveur) pour afficher tout de suite où l'on est.
        const QGeoPositionInfo lastKnown = m_source->lastKnownPosition(false);
        if (lastKnown.isValid() && lastKnown.coordinate().isValid()) {
            m_lat = lastKnown.coordinate().latitude();
            m_lon = lastKnown.coordinate().longitude();
            m_hasApprox = true;
            emit positionUpdated();
        }
        connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                this, [this](const QGeoPositionInfo &info) {
            m_lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
            // v1.0: une position 4G/5G ou WiFi a souvent une précision de
            // plusieurs km. Ne pas la présenter comme un fix GPS (voyant
            // vert, guidage, rotation de carte) : on la garde comme position
            // approximative.
            const bool hasAcc = info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy);
            const double acc = hasAcc
                    ? info.attribute(QGeoPositionInfo::HorizontalAccuracy) : -1.0;
            const bool accurate = !hasAcc || (acc >= 0.0 && acc <= 100.0);
            m_hasFix = info.isValid() && accurate;
            if (info.isValid())
                m_hasApprox = !accurate;
            if (!info.isValid()) {
                emit positionUpdated();
                return;
            }
            QGeoCoordinate c = info.coordinate();
            m_lat = c.latitude();
            m_lon = c.longitude();
            if (!accurate) {
                // Altitude/vitesse/cap d'un fix réseau ne sont pas fiables.
                m_hasAlt = false;
                m_alt = 0.0;
                m_speed = 0.0;
                m_direction = 0.0;
                emit positionUpdated();
                return;
            }
            // Altitude valide seulement si le fix la fournit (sinon 0.0, ce
            // qui fausserait le D+ : un 0 m ne doit jamais compter comme un
            // point bas).
            const double alt = c.altitude();
            m_hasAlt = info.hasAttribute(QGeoPositionInfo::VerticalAccuracy)
                       && info.attribute(QGeoPositionInfo::VerticalAccuracy) >= 0
                       && !std::isnan(alt);
            m_alt = m_hasAlt ? alt : 0.0;
            m_speed = info.hasAttribute(QGeoPositionInfo::GroundSpeed)
                      ? info.attribute(QGeoPositionInfo::GroundSpeed) : 0.0;
            if (m_speed < 0) m_speed = 0.0;
            m_direction = info.hasAttribute(QGeoPositionInfo::Direction)
                          ? info.attribute(QGeoPositionInfo::Direction) : 0.0;
            emit positionUpdated();
        });
        connect(m_source, QOverload<QGeoPositionInfoSource::Error>::of(&QGeoPositionInfoSource::error),
                this, [this](QGeoPositionInfoSource::Error err) {
            qWarning() << "GpsTracker error:" << static_cast<int>(err);
            emit statusChanged();
        });
    }
    return true;
}

void GpsTracker::warmUp()
{
    // v1.0: source créée + dernière position connue, SANS startUpdates() —
    // zéro coût batterie. Appelé au lancement de l'app (Component.onCompleted).
    ensureSource();
}

void GpsTracker::start()
{
    if (m_active)
        return;
    if (!ensureSource())
        return;
    m_source->startUpdates();
    m_active = true;
    m_lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
    m_watchdogTimer.start();
    emit activeChanged();
    emit statusChanged();
}

void GpsTracker::stop()
{
    if (!m_source || !m_active)
        return;
    m_source->stopUpdates();
    m_active = false;
    m_watchdogTimer.stop();
    emit activeChanged();
    emit statusChanged();
}
