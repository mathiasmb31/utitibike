#ifndef SEARCHCLIENT_H
#define SEARCHCLIENT_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Recherche Nominatim (géocodage) côté C++.
// QML XMLHttpRequest ne permet pas de poser un User-Agent fiable sur
// Ubuntu Touch → Nominatim renvoie HTTP 403. QNetworkAccessManager
// contrôle correctement les en-têtes.
class SearchClient : public QObject
{
    Q_OBJECT
public:
    explicit SearchClient(QObject *parent = nullptr);

    // Lance une recherche Nominatim (monde entier).
    // Émet searchFinished(results) ou searchFailed(message).
    Q_INVOKABLE void search(const QString &query);
    // v0.99.1: invalide les réponses en vol (géocodage + inverse).
    Q_INVOKABLE void cancel();

    // Géocodage inverse Nominatim : nomme le lieu le plus proche d'un point
    // (ville/quartier). Émet reverseFinished(place) — place vide si aucun
    // lieu exploitable — ou reverseFailed(). Utilisé pour nommer les trajets
    // enregistrés d'après leur point de départ.
    Q_INVOKABLE void reverse(double lat, double lon);

signals:
    // results : liste de { lat: real, lon: real, display_name: string }
    void searchFinished(const QVariantList &results);
    void searchFailed(const QString &message);
    // place : nom court du lieu (ex. "Toulouse"), vide si introuvable
    void reverseFinished(const QString &place);
    void reverseFailed();

private slots:
    void onReply(QNetworkReply *reply, quint64 seq);
    void onReverseReply(QNetworkReply *reply, quint64 seq);

private:
    QNetworkAccessManager m_nam;
    // v0.99.1: une reponse lente ne doit pas ecraser une recherche plus recente.
    quint64 m_searchSeq = 0;
    quint64 m_reverseSeq = 0;
};

#endif // SEARCHCLIENT_H
