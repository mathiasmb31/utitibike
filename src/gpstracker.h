#ifndef GPSTRACKER_H
#define GPSTRACKER_H

#include <QObject>
#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>
#include <QTimer>

// Expose la position GPS au QML. Sur Ubuntu Touch, le GPS est fourni par
// Lomiri (trust store "location"). Le policy group "location" dans l'AppArmor
// est requis.
class GpsTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(double latitude READ latitude NOTIFY positionUpdated)
    Q_PROPERTY(double longitude READ longitude NOTIFY positionUpdated)
    Q_PROPERTY(double altitude READ altitude NOTIFY positionUpdated)
    Q_PROPERTY(bool hasAltitude READ hasAltitude NOTIFY positionUpdated)
    Q_PROPERTY(double speed READ speed NOTIFY positionUpdated)       // m/s
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY positionUpdated) // km/h
    Q_PROPERTY(double direction READ direction NOTIFY positionUpdated) // degrés
    Q_PROPERTY(bool hasFix READ hasFix NOTIFY positionUpdated)
    // true = position affichée = dernière position connue (pas un fix satellite)
    Q_PROPERTY(bool hasApprox READ hasApprox NOTIFY positionUpdated)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    explicit GpsTracker(QObject *parent = nullptr);

    bool active() const { return m_active; }
    // Disponible = un plugin de source GPS existe. Interroge la liste statique
    // des sources (ne les instancie pas : createDefaultSource, lui, bloque le
    // thread principal au démarrage — la source est créée lazily au start()).
    bool available() const { return !QGeoPositionInfoSource::availableSources().isEmpty(); }
    double latitude() const { return m_lat; }
    double longitude() const { return m_lon; }
    double altitude() const { return m_alt; }
    bool hasAltitude() const { return m_hasAlt; }
    double speed() const { return m_speed; }
    double speedKmh() const { return m_speed * 3.6; }
    double direction() const { return m_direction; }
    bool hasFix() const { return m_hasFix; }
    bool hasApprox() const { return m_hasApprox; }
    QString statusText() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void activeChanged();
    void availableChanged();
    void positionUpdated();
    void statusChanged();

private:
    QGeoPositionInfoSource *m_source = nullptr;
    bool m_active = false;
    double m_lat = 0.0;
    double m_lon = 0.0;
    double m_alt = 0.0;
    bool m_hasAlt = false;
    double m_speed = 0.0;
    double m_direction = 0.0;
    bool m_hasFix = false;
    bool m_hasApprox = false;
};

#endif // GPSTRACKER_H
