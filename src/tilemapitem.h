
#ifndef TILEMAPITEM_H
#define TILEMAPITEM_H

#include <QQuickPaintedItem>
#include <QVariantList>
#include <QPainterPath>

#include "tilecache.h"

class TileMapItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(double centerLat READ centerLat WRITE setCenterLat NOTIFY centerLatChanged)
    Q_PROPERTY(double centerLon READ centerLon WRITE setCenterLon NOTIFY centerLonChanged)
    Q_PROPERTY(int zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // Rotation de la carte en degrés (mode suivi GPS : cap vers le haut)
    Q_PROPERTY(double rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(QVariantList waypoints READ waypoints WRITE setWaypoints NOTIFY waypointsChanged)
    Q_PROPERTY(QVariantList route READ route WRITE setRoute NOTIFY routeChanged)
    Q_PROPERTY(TileCache *cache READ cache WRITE setCache NOTIFY cacheChanged)
    // Position GPS actuelle (point bleu)
    Q_PROPERTY(QVariantMap currentPosition READ currentPosition WRITE setCurrentPosition NOTIFY currentPositionChanged)
    // Tracé du parcours en cours d'enregistrement
    Q_PROPERTY(QVariantList trackPoints READ trackPoints WRITE setTrackPoints NOTIFY trackPointsChanged)
    // Lieux favoris (étoiles dorées)
    Q_PROPERTY(QVariantList favoritePoints READ favoritePoints WRITE setFavoritePoints NOTIFY favoritePointsChanged)

public:
    explicit TileMapItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    double centerLat() const;
    void setCenterLat(double v);
    double centerLon() const;
    void setCenterLon(double v);
    int zoom() const;
    void setZoom(int v);
    double rotation() const { return m_rotation; }
    void setRotation(double v);
    QVariantList waypoints() const;
    void setWaypoints(const QVariantList &w);
    QVariantList route() const;
    void setRoute(const QVariantList &r);
    TileCache *cache() const;
    void setCache(TileCache *c);
    QVariantMap currentPosition() const { return m_currentPosition; }
    void setCurrentPosition(const QVariantMap &p);
    QVariantList trackPoints() const { return m_trackPoints; }
    void setTrackPoints(const QVariantList &t);
    QVariantList favoritePoints() const { return m_favorites; }
    void setFavoritePoints(const QVariantList &f);

    Q_INVOKABLE QVariantMap itemToLatLon(double x, double y) const;
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void zoomAbout(int delta, double x, double y);
    Q_INVOKABLE QVariantMap visibleExtent() const;
    // Construit le QPainterPath de la partie visible d'une polyligne, en
    // coupant les trous (segments anormalement longs). Statique et publique
    // pour être testée unitairement.
    static QPainterPath visiblePath(const QVariantList &points, double ox, double oy,
                                    double world, double w, double h, double margin = 32.0);
    // Ajoute un point au tracé enregistré (incrémental, O(1)) : évite de
    // re-binder la liste complète à chaque fix GPS (serait O(n) par point).
    Q_INVOKABLE void appendTrackPoint(const QVariantMap &p);

signals:
    void centerLatChanged();
    void centerLonChanged();
    void zoomChanged();
    void rotationChanged();
    void waypointsChanged();
    void routeChanged();
    void cacheChanged();
    void currentPositionChanged();
    void trackPointsChanged();
    void favoritePointsChanged();

private:
    double m_centerLat = 43.6044;
    double m_centerLon = 1.4442;
    int m_zoom = 12;
    double m_rotation = 0.0;
    QVariantList m_waypoints;
    QVariantList m_route;
    TileCache *m_cache = nullptr;
    QVariantMap m_currentPosition;
    QVariantList m_trackPoints;
    QVariantList m_favorites;
};

#endif // TILEMAPITEM_H

