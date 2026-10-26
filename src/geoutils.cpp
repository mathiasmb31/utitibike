#include "geoutils.h"
#include "geo.h"

double GeoUtils::haversine(double lat1, double lon1, double lat2, double lon2)
{
    return ::haversine(lat1, lon1, lat2, lon2);
}
