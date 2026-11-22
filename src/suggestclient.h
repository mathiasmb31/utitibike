#ifndef SUGGESTCLIENT_H
#define SUGGESTCLIENT_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>

// Suggestions de tracés à proximité via l'API Overpass :
//  - suggest(lat, lon, mode, radiusM) liste les relations OSM nommées
//    route hiking/foot (mode "foot") ou bicycle/mtb (mode "bike")
//    dans un rayon choisi par l'UI (proportionnel à la durée max, 100 km
//    pour « Toutes ») autour du point donné ;
//  - fetchTrail(id) récupère la géométrie des ways membres de la
//    relation et émet un GPX prêt à être chargé par BrouterClient.
class SuggestClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit SuggestClient(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    // mode : "foot" (randonnée à pied) ou "bike" (vélo)
    Q_INVOKABLE void suggest(double lat, double lon, const QString &mode, int radiusM);
    // v0.99.1: force une nouvelle requête (ignore le cache 10 min).
    Q_INVOKABLE void refresh();
    // v0.99.1: invalide les réponses en vol et arrête l'indicateur.
    Q_INVOKABLE void cancel();
    // Charge la géométrie complète de la relation et émet trailReady(gpx)
    // type = "relation" ou "way" (les suggestions incluent les deux)
    Q_INVOKABLE void fetchTrail(qint64 id, const QString &type = QStringLiteral("relation"));

signals:
    void busyChanged();
    // items : [{ id, name, route, lat, lon, distM }], triés par distance croissante
    void suggestionsReady(const QVariantList &items);
    void suggestionsFailed(const QString &message);
    void trailReady(const QString &gpx);
    void trailFailed(const QString &message);

private:
    void setBusy(bool v);
    void query(const QString &overpass, int timeoutMs,
               void (SuggestClient::*onSuccess)(const QByteArray &),
               void (SuggestClient::*onError)(const QString &),
               int mirror = 0);   // v0.99.1: fallback entre miroirs Overpass
    void onSuggestReply(const QByteArray &body);
    void onSuggestError(const QString &msg);
    void onTrailReply(const QByteArray &body);
    void onTrailError(const QString &msg);
    QString trailGpx(const QJsonDocument &doc) const;
    QString cacheKey() const;

    QNetworkAccessManager m_nam;
    bool m_busy = false;
    double m_lat = 0.0;
    double m_lon = 0.0;
    QString m_mode = QStringLiteral("bike");
    int m_radiusM = 0;
    // Cache des suggestions (10 min) pour éviter de relancer Overpass à
    // chaque ouverture du panneau / changement de filtre aller-retour.
    QString m_cacheKey;
    QVariantList m_cacheItems;
    qint64 m_cacheAtMs = 0;
    // id de relation -> nom affiché (retenu entre suggest() et fetchTrail())
    QHash<qint64, QString> m_names;
    QHash<qint64, QString> m_types;   // "relation" ou "way"
    qint64 m_pendingId = 0;
    quint64 m_querySeq = 0;  // v0.99.1: ignore les reponses Overpass perimees
};

#endif // SUGGESTCLIENT_H
