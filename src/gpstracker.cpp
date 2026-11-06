#include "gpstracker.h"
#include <QDebug>
#include <cmath>

GpsTracker::GpsTracker(QObject *parent)
    : QObject(parent)
{
    // La source GPS n'est PAS créée ici : QGeoPositionInfoSource::createDefaultSource
    // peut bloquer le thread principal au démarrage (écran blanc sur UT).
    // Elle est créée lazily au premier start().
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

void GpsTracker::start()
{
    if (m_active)
        return;
    // Création lazy de la source GPS (peut prendre du temps)
    if (!m_source) {
        m_source = QGeoPositionInfoSource::createDefaultSource(this);
        if (!m_source) {
            qWarning() << "GpsTracker: no position source available";
            emit statusChanged();
            return;
        }
        m_source->setUpdateInterval(1000);
        // Applique le mode choisi (auto = satellite + réseau WiFi/cell)
        setPositioningMode(m_positioningMode);
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
            m_hasFix = info.isValid();
            if (m_hasFix)
                m_hasApprox = false;  // un vrai fix remplace la position approx
            if (!m_hasFix) {
                // Émettre quand même : le QML masque la position (hasFix=false)
                // au lieu de garder un point fantôme à la dernière position.
                emit positionUpdated();
                return;
            }
            QGeoCoordinate c = info.coordinate();
            m_lat = c.latitude();
            m_lon = c.longitude();
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
        emit availableChanged();
    }
    m_source->startUpdates();
    m_active = true;
    emit activeChanged();
    emit statusChanged();
}

void GpsTracker::stop()
{
    if (!m_source || !m_active)
        return;
    m_source->stopUpdates();
    m_active = false;
    emit activeChanged();
    emit statusChanged();
}

void GpsTracker::setPositioningMode(const QString &mode)
{
    m_positioningMode = (mode == QLatin1String("satellite") || mode == QLatin1String("network"))
                        ? mode : QStringLiteral("auto");
    if (!m_source)
        return;
    if (m_positioningMode == QLatin1String("satellite"))
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
    else if (m_positioningMode == QLatin1String("network")) {
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::NonSatellitePositioningMethods);
        // Secours : pas de fix réseau après 30 s → toutes méthodes (satellite
        // inclus), sinon aucune position en mode « réseau » sur certains
        // appareils où le provider réseau est indisponible.
        QTimer::singleShot(30000, this, [this]() {
            if (!m_hasFix) {
                qWarning() << "GpsTracker: mode réseau sans fix après 30 s — secours satellite";
                m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
            }
        });
    } else
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
}
