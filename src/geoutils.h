#ifndef GEOUTILS_H
#define GEOUTILS_H

#include <QObject>

// Fonctions géo exposées au QML via le singleton `GeoUtils`
// (import Utitibike 1.0). Évite de redéfinir haversine en JS dans le QML.
class GeoUtils : public QObject
{
    Q_OBJECT
public:
    explicit GeoUtils(QObject *parent = nullptr) : QObject(parent) {}

    // Distance orthodromique (haversine) en mètres entre deux points GPS.
    Q_INVOKABLE static double haversine(double lat1, double lon1,
                                       double lat2, double lon2);
};

#endif // GEOUTILS_H
