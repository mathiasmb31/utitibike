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
//  - suggest(lat, lon, mode) liste les relations OSM nommées
//    route hiking/foot (mode "foot") ou bicycle/mtb (mode "bike")
//    dans un rayon de 5 km autour du point donné ;
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
    Q_INVOKABLE void suggest(double lat, double lon, const QString &mode);
    // Charge la géométrie complète de la relation et émet trailReady(gpx)
    Q_INVOKABLE void fetchTrail(qint64 relationId);

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
               void (SuggestClient::*onError)(const QString &));
    void onSuggestReply(const QByteArray &body);
    void onSuggestError(const QString &msg);
    void onTrailReply(const QByteArray &body);
    void onTrailError(const QString &msg);
    QString trailGpx(const QJsonDocument &doc) const;

    QNetworkAccessManager m_nam;
    bool m_busy = false;
    double m_lat = 0.0;
    double m_lon = 0.0;
    QString m_mode = QStringLiteral("bike");
    // id de relation -> nom affiché (retenu entre suggest() et fetchTrail())
    QHash<qint64, QString> m_names;
    qint64 m_pendingId = 0;
};

#endif // SUGGESTCLIENT_H
