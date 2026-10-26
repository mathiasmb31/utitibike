
#include "tilemapitem.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantMap>
#include <QtMath>

namespace {

const int kMinZoom = 3;
const int kMaxZoom = 17;
const double kTilePx = 256.0;

double worldSize(int z)
{
    return kTilePx * std::pow(2.0, z);
}

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

double worldXToLon(double x, double world)
{
    return x / world * 360.0 - 180.0;
}

double worldYToLat(double y, double world)
{
    const double n = M_PI * (1.0 - 2.0 * y / world);
    return qRadiansToDegrees(std::atan(std::sinh(n)));
}

double wrapLon(double lon)
{
    while (lon > 180.0)
        lon -= 360.0;
    while (lon < -180.0)
        lon += 360.0;
    return lon;
}

double clampLat(double lat)
{
    return qBound(-85.0511, lat, 85.0511);
}

// Construit le QPainterPath d'une polyligne en ne gardant que les segments
// visibles à l'écran (marge de 32 px) : un tracé de dizaines de milliers de
// points (itinéraire BRouter, parcours enregistré) n'est plus dessiné en
// entier à chaque frame. Le voisin d'un point visible est toujours inclus,
// même hors champ, pour que la ligne entre/sorte correctement par le bord.
QPainterPath visiblePath(const QVariantList &points, double ox, double oy,
                         double world, double w, double h, double margin = 32.0)
{
    QPainterPath path;
    const double minWX = ox - margin;
    const double maxWX = ox + w + margin;
    const double minWY = oy - margin;
    const double maxWY = oy + h + margin;
    const int n = points.size();
    int prev = -1;
    for (int i = 0; i < n; ++i) {
        const QVariantMap m = points.at(i).toMap();
        const double wx = lonToWorldX(m.value(QStringLiteral("lon")).toDouble(), world);
        const double wy = latToWorldY(m.value(QStringLiteral("lat")).toDouble(), world);
        if (wx < minWX || wx > maxWX || wy < minWY || wy > maxWY)
            continue;
        const QPointF pt(wx - ox, wy - oy);
        if (prev == i - 1) {
            path.lineTo(pt);
        } else if (i > 0) {
            // Nouveau sous-chemin : repart du voisin précédent (peut être hors
            // champ) pour tracer le segment jusqu'au bord.
            const QVariantMap pm = points.at(i - 1).toMap();
            path.moveTo(lonToWorldX(pm.value(QStringLiteral("lon")).toDouble(), world) - ox,
                        latToWorldY(pm.value(QStringLiteral("lat")).toDouble(), world) - oy);
            path.lineTo(pt);
        } else {
            path.moveTo(pt);
        }
        prev = i;
    }
    return path;
}

} // namespace

TileMapItem::TileMapItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

double TileMapItem::centerLat() const
{
    return m_centerLat;
}

void TileMapItem::setCenterLat(double v)
{
    const double nv = clampLat(v);
    if (m_centerLat == nv)
        return;
    m_centerLat = nv;
    emit centerLatChanged();
    update();
}

double TileMapItem::centerLon() const
{
    return m_centerLon;
}

void TileMapItem::setCenterLon(double v)
{
    const double nv = wrapLon(v);
    if (m_centerLon == nv)
        return;
    m_centerLon = nv;
    emit centerLonChanged();
    update();
}

int TileMapItem::zoom() const
{
    return m_zoom;
}

void TileMapItem::setZoom(int v)
{
    const int nv = qBound(kMinZoom, v, kMaxZoom);
    if (m_zoom == nv)
        return;
    m_zoom = nv;
    emit zoomChanged();
    update();
}

QVariantList TileMapItem::waypoints() const
{
    return m_waypoints;
}

void TileMapItem::setWaypoints(const QVariantList &w)
{
    if (m_waypoints == w)
        return;
    m_waypoints = w;
    emit waypointsChanged();
    update();
}

QVariantList TileMapItem::route() const
{
    return m_route;
}

void TileMapItem::setRoute(const QVariantList &r)
{
    if (m_route == r)
        return;
    m_route = r;
    emit routeChanged();
    update();
}

TileCache *TileMapItem::cache() const
{
    return m_cache;
}

void TileMapItem::setCache(TileCache *c)
{
    if (m_cache == c)
        return;
    if (m_cache)
        disconnect(m_cache, &TileCache::tileReady, this, nullptr);
    m_cache = c;
    if (m_cache)
        connect(m_cache, &TileCache::tileReady, this, [this]() { update(); });
    emit cacheChanged();
    update();
}

void TileMapItem::setCurrentPosition(const QVariantMap &p)
{
    if (m_currentPosition == p)
        return;
    m_currentPosition = p;
    emit currentPositionChanged();
    update();
}

void TileMapItem::setTrackPoints(const QVariantList &t)
{
    if (m_trackPoints == t)
        return;
    m_trackPoints = t;
    emit trackPointsChanged();
    update();
}

void TileMapItem::appendTrackPoint(const QVariantMap &p)
{
    m_trackPoints.append(p);
    emit trackPointsChanged();
    update();
}

void TileMapItem::setFavoritePoints(const QVariantList &f)
{
    if (m_favorites == f)
        return;
    m_favorites = f;
    emit favoritePointsChanged();
    update();
}

QVariantMap TileMapItem::itemToLatLon(double x, double y) const
{
    const double world = worldSize(m_zoom);
    const double ox = lonToWorldX(m_centerLon, world) - width() / 2.0;
    const double oy = latToWorldY(m_centerLat, world) - height() / 2.0;
    QVariantMap m;
    m.insert(QStringLiteral("lat"), clampLat(worldYToLat(oy + y, world)));
    m.insert(QStringLiteral("lon"), wrapLon(worldXToLon(ox + x, world)));
    return m;
}

void TileMapItem::panBy(double dx, double dy)
{
    if (dx == 0.0 && dy == 0.0)
        return;
    const double world = worldSize(m_zoom);
    setCenterLon(wrapLon(worldXToLon(lonToWorldX(m_centerLon, world) - dx, world)));
    setCenterLat(clampLat(worldYToLat(latToWorldY(m_centerLat, world) - dy, world)));
}

void TileMapItem::zoomAbout(int delta, double x, double y)
{
    const int nz = qBound(kMinZoom, m_zoom + delta, kMaxZoom);
    if (nz == m_zoom)
        return;

    const double world0 = worldSize(m_zoom);
    const double world1 = worldSize(nz);
    const double ox = lonToWorldX(m_centerLon, world0) - width() / 2.0;
    const double oy = latToWorldY(m_centerLat, world0) - height() / 2.0;

    const double lon = wrapLon(worldXToLon(ox + x, world0));
    const double lat = clampLat(worldYToLat(oy + y, world0));

    m_zoom = nz;
    emit zoomChanged();

    const double px = lonToWorldX(lon, world1);
    const double py = latToWorldY(lat, world1);
    setCenterLon(wrapLon(worldXToLon(px - (x - width() / 2.0), world1)));
    setCenterLat(clampLat(worldYToLat(py - (y - height() / 2.0), world1)));
    update();
}

QVariantMap TileMapItem::visibleExtent() const
{
    const QVariantMap tl = itemToLatLon(0.0, 0.0);
    const QVariantMap br = itemToLatLon(width(), height());
    QVariantMap m;
    m.insert(QStringLiteral("lat1"), qMin(tl.value(QStringLiteral("lat")).toDouble(),
                                          br.value(QStringLiteral("lat")).toDouble()));
    m.insert(QStringLiteral("lon1"), qMin(tl.value(QStringLiteral("lon")).toDouble(),
                                          br.value(QStringLiteral("lon")).toDouble()));
    m.insert(QStringLiteral("lat2"), qMax(tl.value(QStringLiteral("lat")).toDouble(),
                                          br.value(QStringLiteral("lat")).toDouble()));
    m.insert(QStringLiteral("lon2"), qMax(tl.value(QStringLiteral("lon")).toDouble(),
                                          br.value(QStringLiteral("lon")).toDouble()));
    return m;
}

void TileMapItem::paint(QPainter *painter)
{
    const double world = worldSize(m_zoom);
    const double ox = lonToWorldX(m_centerLon, world) - width() / 2.0;
    const double oy = latToWorldY(m_centerLat, world) - height() / 2.0;
    const int n = 1 << m_zoom;

    painter->fillRect(boundingRect(), QColor(QStringLiteral("#e0dcd4")));

    const int tx0 = qFloor(ox / kTilePx);
    const int tx1 = qFloor((ox + width()) / kTilePx);
    const int ty0 = qFloor(oy / kTilePx);
    const int ty1 = qFloor((oy + height()) / kTilePx);

    for (int ty = ty0; ty <= ty1; ++ty) {
        if (ty < 0 || ty >= n)
            continue;
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int wx = ((tx % n) + n) % n;
            QImage img;
            if (m_cache)
                img = m_cache->tile(m_zoom, wx, ty);
            const QRectF dest(tx * kTilePx - ox, ty * kTilePx - oy, kTilePx, kTilePx);
            if (!img.isNull())
                painter->drawImage(dest, img);
            else
                painter->fillRect(dest, QColor(QStringLiteral("#cfc9c0")));
        }
    }

    if (m_route.size() > 1) {
        // drawPath sur un path vide est un no-op : pas de return ici, la suite
        // du paint (waypoints, position GPS, favoris, attribution) doit suivre.
        const QPainterPath path = visiblePath(m_route, ox, oy, world,
                                             width(), height());
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen shadow(QColor(0, 0, 0, 70));
        shadow.setWidthF(9.0);
        shadow.setCapStyle(Qt::RoundCap);
        shadow.setJoinStyle(Qt::RoundJoin);
        painter->setPen(shadow);
        painter->drawPath(path);
        QPen pen(QColor(QStringLiteral("#1278d0")));
        pen.setWidthF(5.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->drawPath(path);
        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    if (!m_waypoints.isEmpty()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        int idx = 0;
        for (const QVariant &v : m_waypoints) {
            const QVariantMap m = v.toMap();
            const QPointF pt(lonToWorldX(m.value(QStringLiteral("lon")).toDouble(), world) - ox,
                             latToWorldY(m.value(QStringLiteral("lat")).toDouble(), world) - oy);
            QColor fill;
            if (idx == 0)
                fill = QColor(QStringLiteral("#2e8b57"));
            else if (idx == m_waypoints.size() - 1)
                fill = QColor(QStringLiteral("#e95420"));
            else
                fill = QColor(QStringLiteral("#f99b11"));
            // Ombre portée légère pour le contraste
            painter->setPen(QPen(QColor(0, 0, 0, 60), 1.0));
            painter->setBrush(QColor(0, 0, 0, 40));
            painter->drawEllipse(pt + QPointF(1.5, 2.0), 16.0, 16.0);
            // Cercle principal avec contour blanc épais
            painter->setPen(QPen(Qt::white, 4.5));
            painter->setBrush(fill);
            painter->drawEllipse(pt, 16.0, 16.0);
            // Numéro du waypoint
            painter->setPen(QPen(Qt::white, 1.0));
            QFont numFont = painter->font();
            numFont.setPointSizeF(8.0);
            numFont.setBold(true);
            painter->setFont(numFont);
            QString label;
            if (idx == 0)
                label = QStringLiteral("D");
            else if (idx == m_waypoints.size() - 1)
                label = QStringLiteral("A");
            else
                label = QString::number(idx);
            painter->drawText(QRectF(pt.x() - 16.0, pt.y() - 16.0, 32.0, 32.0),
                              Qt::AlignCenter, label);
            ++idx;
        }
        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    // Tracé du parcours enregistré (ligne verte)
    if (m_trackPoints.size() > 1) {
        const QPainterPath trackPath = visiblePath(m_trackPoints, ox, oy, world,
                                                   width(), height());
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen trackPen(QColor(QStringLiteral("#2e8b57")));
        trackPen.setWidthF(4.0);
        trackPen.setCapStyle(Qt::RoundCap);
        trackPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(trackPen);
        painter->drawPath(trackPath);
        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    // Position GPS actuelle (point bleu avec halo)
    if (!m_currentPosition.isEmpty()) {
        const double lat = m_currentPosition.value(QStringLiteral("lat")).toDouble();
        const double lon = m_currentPosition.value(QStringLiteral("lon")).toDouble();
        const QPointF pt(lonToWorldX(lon, world) - ox,
                         latToWorldY(lat, world) - oy);
        painter->setRenderHint(QPainter::Antialiasing, true);
        // Halo extérieur (translucide)
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(18, 120, 208, 60));
        painter->drawEllipse(pt, 18.0, 18.0);
        // Cercle intérieur
        painter->setBrush(QColor(QStringLiteral("#1278d0")));
        painter->setPen(QPen(Qt::white, 2.5));
        painter->drawEllipse(pt, 9.0, 9.0);
        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    // Lieux favoris (étoiles dorées)
    if (!m_favorites.isEmpty()) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        for (const QVariant &v : m_favorites) {
            const QVariantMap m = v.toMap();
            const QPointF pt(lonToWorldX(m.value(QStringLiteral("lon")).toDouble(), world) - ox,
                             latToWorldY(m.value(QStringLiteral("lat")).toDouble(), world) - oy);
            // Étoile à 5 branches
            QPainterPath star;
            const double r1 = 8.0, r2 = 3.5;
            for (int i = 0; i < 10; ++i) {
                const double r = (i % 2 == 0) ? r1 : r2;
                const double angle = -M_PI / 2.0 + i * M_PI / 5.0;
                const double px = pt.x() + r * std::cos(angle);
                const double py = pt.y() + r * std::sin(angle);
                if (i == 0) star.moveTo(px, py);
                else star.lineTo(px, py);
            }
            star.closeSubpath();
            painter->setPen(QPen(QColor(180, 140, 0), 1.5));
            painter->setBrush(QColor(QStringLiteral("#ffd700")));
            painter->drawPath(star);
        }
        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    painter->setPen(QColor(80, 80, 80, 200));
    QFont f = painter->font();
    f.setPointSizeF(7.0);
    painter->setFont(f);
    // Attribution selon la source des tuiles (ODbL pour OSM/CyclOSM/CARTO,
    // crédits imagery pour Esri).
    const QString server = m_cache ? m_cache->tileServer() : QString();
    QString attribution;
    if (server == QLatin1String("satellite"))
        attribution = QStringLiteral("© Esri, Maxar, Earthstar Geographics");
    else if (server == QLatin1String("dark"))
        attribution = QStringLiteral("© OpenStreetMap contributors · CARTO");
    else
        attribution = QStringLiteral("© OpenStreetMap contributors");
    painter->drawText(QRectF(6.0, height() - 16.0, 400.0, 12.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      attribution);
}

