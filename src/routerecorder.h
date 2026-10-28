#ifndef ROUTERECORDER_H
#define ROUTERECORDER_H

#include <QObject>
#include <QVariantList>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>

class GpsTracker;

// Enregistre un parcours GPS depuis le GpsTracker. Accumule les positions,
// calcule distance / D+ / durée / vitesse moyenne. Sauvegarde en GPX.
class RouteRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double distanceM READ distanceM NOTIFY statsChanged)
    Q_PROPERTY(double ascentM READ ascentM NOTIFY statsChanged)
    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY statsChanged)
    Q_PROPERTY(double avgSpeedKmh READ avgSpeedKmh NOTIFY statsChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY statsChanged)
    // Liste des points enregistrés, bindable depuis le QML. L'append + notify
    // sont faits côté C++ : un concat JS à chaque point GPS serait O(n²).
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)

public:
    explicit RouteRecorder(GpsTracker *gps, QObject *parent = nullptr);

    bool recording() const { return m_recording; }
    double distanceM() const { return m_distance; }
    double ascentM() const { return m_ascent; }
    int elapsedSec() const;
    double avgSpeedKmh() const;
    int pointCount() const { return m_points.size(); }
    QVariantList points() const { return m_points; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();  // vide les points et les stats (sans toucher à recording)
    Q_INVOKABLE QString saveGpx() const;  // retourne l'URL du fichier, vide si échec
    // Écrit le GPX sous un nom imposé (auto-sauvegarde périodique).
    QString saveGpx(const QString &fileName) const;
    // Génère le contenu GPX en mémoire (pour l'historique, sans écrire de fichier).
    Q_INVOKABLE QString gpxContent() const;

signals:
    void recordingChanged();
    void statsChanged();
    void pointsChanged();
    // Émis à chaque ajout d'un point (incrémental) : évite au QML de copier
    // la liste complète (O(n)) à chaque fix GPS via le binding points.
    void pointAdded(const QVariantMap &point);

private:
    GpsTracker *m_gps;
    bool m_recording = false;
    QVariantList m_points;
    double m_distance = 0.0;
    double m_ascent = 0.0;
    QElapsedTimer m_elapsed;
    QDateTime m_startTime;
    double m_lastLat = 0.0;
    double m_lastLon = 0.0;
    double m_lastAlt = 0.0;
    bool m_hasAlt = false;
    bool m_hasLast = false;
    QTimer m_autoSaveTimer;   // sauvegarde périodique pendant l'enregistrement
};

#endif // ROUTERECORDER_H
