#ifndef ROUTEHISTORY_H
#define ROUTEHISTORY_H

#include <QObject>
#include <QVariantList>

// Stocke l'historique des itinéraires calculés dans un JSON persistant.
// Chaque entrée : nom, date, distance, D+, waypoints, et gpxFile = nom du
// fichier GPX externalisé dans <AppData>/history/ (le JSON ne contient pas
// le texte GPX, qui peut atteindre des mégaoctets).
class RouteHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)

public:
    explicit RouteHistory(QObject *parent = nullptr);

    QVariantList entries() const { return m_entries; }

    // Ajoute un itinéraire à l'historique (appelé quand routeReady)
    Q_INVOKABLE void addEntry(const QString &name, double distanceM, double ascentM,
                              const QVariantList &waypoints, const QString &gpx);
    Q_INVOKABLE void removeEntry(int index);
    Q_INVOKABLE void clear();
    // Retourne le GPX d'une entrée, ou QString vide
    Q_INVOKABLE QString gpxAt(int index) const;
    Q_INVOKABLE QVariantList waypointsAt(int index) const;

signals:
    void entriesChanged();

private:
    void load();
    void save();
    QString writeGpxFile(const QString &gpx);          // retourne le nom du fichier, vide si échec
    QString gpxFilePath(const QString &fileName) const;

    QVariantList m_entries;
};

#endif // ROUTEHISTORY_H
