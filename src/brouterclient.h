
#ifndef BROUTERCLIENT_H
#define BROUTERCLIENT_H

#include <QObject>
#include <QVariantList>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class BrouterClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(QString lastGpx READ lastGpx NOTIFY lastGpxChanged)
    // BRouter uphill cost: higher avoids hills (flatter route),
    // negative seeks elevation (more climbing). 0 ignores elevation.
    Q_PROPERTY(int uphillCost READ uphillCost WRITE setUphillCost NOTIFY uphillCostChanged)
    Q_PROPERTY(bool navigation READ navigation NOTIFY navigationChanged)
    // Prochaine instruction de virage (ex: "Dans 200 m, tourner à gauche")
    Q_PROPERTY(QString nextInstruction READ nextInstruction NOTIFY nextInstructionChanged)
    Q_PROPERTY(double nextInstructionDistance READ nextInstructionDistance NOTIFY nextInstructionChanged)

public:
    explicit BrouterClient(QObject *parent = nullptr);

    bool busy() const;
    QString profile() const;
    void setProfile(const QString &p);
    QString lastGpx() const;
    int uphillCost() const;
    void setUphillCost(int v);
    bool navigation() const { return m_navigation; }
    QString nextInstruction() const { return m_nextInstruction; }
    double nextInstructionDistance() const { return m_nextInstructionDistance; }

    Q_INVOKABLE void startNavigation();
    Q_INVOKABLE void stopNavigation();
    // Appelé par le QML quand la position GPS change, pour mettre à jour
    // l'instruction courante et détecter l'arrivée aux waypoints.
    Q_INVOKABLE void updatePosition(double lat, double lon);

    Q_INVOKABLE void requestRoute(const QVariantList &waypoints);
    Q_INVOKABLE QString exportGpx(const QString &gpx) const;
    Q_INVOKABLE void loadGpx(const QString &fileUrl);
    Q_INVOKABLE void loadGpxData(const QString &gpxContent);

signals:
    void busyChanged();
    void profileChanged();
    void lastGpxChanged();
    void uphillCostChanged();
    void routeReady(const QVariantList &points, double distanceM, double ascentM);
    void routeFailed(const QString &message);
    void navigationChanged();
    void nextInstructionChanged();
    void routeFinished();
    // Émis quand on approche d'un virage : severity 0=200m, 1=100m, 2=50m
    void turnAlert(int severity);

private:
    void parseGpx(const QString &gpx);
    void setBusy(bool v);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
    QString m_profile;
    QString m_lastGpx;
    int m_uphillCost = 50;

    // Navigation turn-by-turn
    bool m_navigation = false;
    QVariantList m_routePoints;       // tous les points de l'itinéraire
    QVariantList m_turnPoints;        // points de virage avec instructions
    QString m_nextInstruction;
    double m_nextInstructionDistance = 0.0;
    // Seuils d'alerte virage déjà atteints (évite les vibrations répétées)
    bool m_alert200 = false;
    bool m_alert100 = false;
    bool m_alert50 = false;
};

#endif // BROUTERCLIENT_H

