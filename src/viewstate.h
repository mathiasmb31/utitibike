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
    // Préférence de thème : "auto" (suit le système Lomiri), "dark", "light"
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // Source de position GPS : "auto", "satellite" (GPS seul), "network" (WiFi/cell)
    Q_PROPERTY(QString positioningMode READ positioningMode WRITE setPositioningMode NOTIFY positioningModeChanged)
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
    QString themeMode() const { return m_themeMode; }
    void setThemeMode(const QString &v);
    QString positioningMode() const { return m_positioningMode; }
    void setPositioningMode(const QString &v);
    bool restored() const { return m_restored; }

signals:
    void centerLatChanged();
    void centerLonChanged();
    void zoomChanged();
    void themeModeChanged();
    void positioningModeChanged();

private:
    void load();
    void scheduleSave();

    double m_lat = 43.6044;   // défaut : Toulouse (comme TileMapItem)
    double m_lon = 1.4442;
    int m_zoom = 12;
    QString m_themeMode = QStringLiteral("auto");
    QString m_positioningMode = QStringLiteral("auto");
    bool m_restored = false;
    QTimer m_saveTimer;
};

#endif // VIEWSTATE_H
