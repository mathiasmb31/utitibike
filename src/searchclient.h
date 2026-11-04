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
    void onReply(QNetworkReply *reply);
    void onReverseReply(QNetworkReply *reply);

private:
    QNetworkAccessManager m_nam;
};

#endif // SEARCHCLIENT_H
