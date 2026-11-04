
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
    // true = pénalise fortement les chemins non goudronnés (paramètre
    // unpavedPenalty du profil BRouter, ignoré par les profils qui ne
    // le définissent pas).
    Q_PROPERTY(bool avoidUnpaved READ avoidUnpaved WRITE setAvoidUnpaved NOTIFY avoidUnpavedChanged)
    Q_PROPERTY(bool navigation READ navigation NOTIFY navigationChanged)
    // Prochaine instruction de virage (ex: "Dans 200 m, tourner à gauche")
    Q_PROPERTY(QString nextInstruction READ nextInstruction NOTIFY nextInstructionChanged)
    Q_PROPERTY(double nextInstructionDistance READ nextInstructionDistance NOTIFY nextInstructionChanged)
    // Distance restante le long de l'itinéraire depuis la position courante (m)
    Q_PROPERTY(double remainingDistanceM READ remainingDistanceM NOTIFY nextInstructionChanged)
    // true pendant un recalcul automatique (hors itinéraire) : la route
    // recalculée ne doit pas être re-sauvegardée dans l'historique
    Q_PROPERTY(bool recalculating READ recalculating WRITE setRecalculating NOTIFY recalculatingChanged)
    // Vrai pendant le chargement d'un GPX depuis l'historique
    // (évite le re-sauvegarde automatique dans l'historique)
    Q_PROPERTY(bool loadingFromHistory READ loadingFromHistory WRITE setLoadingFromHistory NOTIFY loadingFromHistoryChanged)

public:
    explicit BrouterClient(QObject *parent = nullptr);

    bool busy() const;
    QString profile() const;
    void setProfile(const QString &p);
    QString lastGpx() const;
    int uphillCost() const;
    void setUphillCost(int v);
    bool avoidUnpaved() const { return m_avoidUnpaved; }
    void setAvoidUnpaved(bool v);
    bool navigation() const { return m_navigation; }
    QString nextInstruction() const { return m_nextInstruction; }
    double nextInstructionDistance() const { return m_nextInstructionDistance; }
    double remainingDistanceM() const { return m_remainingDistanceM; }
    bool recalculating() const { return m_recalculating; }
    void setRecalculating(bool v);
    bool loadingFromHistory() const { return m_loadingFromHistory; }
    void setLoadingFromHistory(bool v);

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
    void avoidUnpavedChanged();
    void routeReady(const QVariantList &points, double distanceM, double ascentM);
    void routeFailed(const QString &message);
    void navigationChanged();
    void nextInstructionChanged();
    void routeFinished();
    // Émis quand on approche d'un virage : severity 0=200m, 1=100m, 2=50m
    void turnAlert(int severity);
    // Émis quand la position courante s'écarte de plus de 60 m du tracé
    // (déclenche un recalcul d'itinéraire côté QML ; débounce 30 s)
    void offRoute(double distanceM);
    void loadingFromHistoryChanged();
    void recalculatingChanged();

private:
    void parseGpx(const QString &gpx);
    void setBusy(bool v);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
    QString m_profile;
    QString m_lastGpx;
    int m_uphillCost = 50;
    bool m_avoidUnpaved = false;

    // Navigation turn-by-turn
    bool m_navigation = false;
    QVariantList m_routePoints;       // tous les points de l'itinéraire
    QVariantList m_turnPoints;        // points de virage avec instructions
    QString m_nextInstruction;
    double m_nextInstructionDistance = 0.0;
    double m_remainingDistanceM = 0.0;
    bool m_recalculating = false;
    bool m_loadingFromHistory = false;
    // Seuils d'alerte virage déjà atteints (évite les vibrations répétées)
    bool m_alert200 = false;
    bool m_alert100 = false;
    bool m_alert50 = false;
    // Debounce « hors itinéraire » : hystérésis 30 m / 15 m, max 1 signal / 30 s
    bool m_offRouteActive = false;
    qint64 m_lastOffRouteMs = 0;
};

#endif // BROUTERCLIENT_H

