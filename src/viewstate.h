#ifndef VIEWSTATE_H
#define VIEWSTATE_H

#include <QObject>
#include <QTimer>

// Persiste la dernière vue de la carte (centre + zoom) dans
// <AppData>/view.json et la restaure au démarrage. La sauvegarde est
// debouncée (1 s) : un pan continu n'écrit pas le fichier à chaque frame.
class ViewState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double centerLat READ centerLat WRITE setCenterLat NOTIFY centerLatChanged)
    Q_PROPERTY(double centerLon READ centerLon WRITE setCenterLon NOTIFY centerLonChanged)
    Q_PROPERTY(int zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // true si une vue sauvegardée a été restaurée. Sinon (premier lancement)
    // le QML centre la carte sur le premier fix GPS.
    Q_PROPERTY(bool restored READ restored CONSTANT)

public:
    explicit ViewState(QObject *parent = nullptr);

    double centerLat() const { return m_lat; }
    void setCenterLat(double v);
    double centerLon() const { return m_lon; }
    void setCenterLon(double v);
    int zoom() const { return m_zoom; }
    void setZoom(int v);
    bool restored() const { return m_restored; }

signals:
    void centerLatChanged();
    void centerLonChanged();
    void zoomChanged();

private:
    void load();
    void scheduleSave();

    double m_lat = 43.6044;   // défaut : Toulouse (comme TileMapItem)
    double m_lon = 1.4442;
    int m_zoom = 12;
    bool m_restored = false;
    QTimer m_saveTimer;
};

#endif // VIEWSTATE_H
