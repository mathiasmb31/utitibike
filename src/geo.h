#ifndef GEO_H
#define GEO_H

#include <QtMath>
#include <algorithm>
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
    // v0.99.1: pour des points quasi-antipodes, l'arrondi peut donner
    // a > 1 -> sqrt(1-a) = NaN -> distance NaN (filtre GPS, D+, stats).
    const double clamped = std::min(1.0, std::max(0.0, a));
    return 2.0 * r * std::atan2(std::sqrt(clamped), std::sqrt(1.0 - clamped));
}

#endif // GEO_H
