#ifndef GEO_H
#define GEO_H

#include <QtMath>
#include <cmath>

// Distance orthodromique (formule de haversine) en mètres entre deux points
// GPS. Implémentation unique partagée par tout le C++ (brouter, recorder,
// favorites) et reprise par GeoUtils pour le QML.
inline double haversine(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371000.0;
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0)
                   + std::cos(qDegreesToRadians(lat1)) * std::cos(qDegreesToRadians(lat2))
                   * std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    return 2.0 * r * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

#endif // GEO_H
