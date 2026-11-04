#include "searchclient.h"
#include "appdata.h"

#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

SearchClient::SearchClient(QObject *parent)
    : QObject(parent)
{
}

void SearchClient::search(const QString &query)
{
    if (query.trimmed().length() < 3) {
        emit searchFailed(tr("Requête trop courte"));
        return;
    }

    QUrl url(QStringLiteral("https://nominatim.openstreetmap.org/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("8"));
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest req(url);
    // Nominatim exige un User-Agent identifiable (sinon HTTP 403)
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("utitibike/") + QStringLiteral(UTITIBIKE_VERSION)
                  + QStringLiteral(" (Ubuntu Touch)"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam.get(req);

    // Timeout 15 s
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(15000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void SearchClient::onReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString msg = tr("Recherche indisponible (%1)").arg(reply->errorString());
        qWarning() << "SearchClient error:" << reply->error() << reply->errorString();
        emit searchFailed(msg);
        return;
    }

    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus != 200) {
        const QString msg = tr("Recherche indisponible (HTTP %1)").arg(httpStatus);
        qWarning() << "SearchClient HTTP" << httpStatus << body.left(200);
        emit searchFailed(msg);
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "SearchClient JSON parse error:" << parseErr.errorString();
        emit searchFailed(tr("Réponse illisible"));
        return;
    }

    QVariantList results;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        bool latOk = false, lonOk = false;
        const double lat = o.value(QStringLiteral("lat")).toString().toDouble(&latOk);
        const double lon = o.value(QStringLiteral("lon")).toString().toDouble(&lonOk);
        if (!latOk || !lonOk)
            continue;
        QVariantMap r;
        r.insert(QStringLiteral("lat"), lat);
        r.insert(QStringLiteral("lon"), lon);
        r.insert(QStringLiteral("display_name"), o.value(QStringLiteral("display_name")).toString());
        results.append(r);
    }

    emit searchFinished(results);
}

void SearchClient::reverse(double lat, double lon)
{
    QUrl url(QStringLiteral("https://nominatim.openstreetmap.org/reverse"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("jsonv2"));
    q.addQueryItem(QStringLiteral("lat"), QString::number(lat, 'f', 6));
    q.addQueryItem(QStringLiteral("lon"), QString::number(lon, 'f', 6));
    // zoom 12 ≈ quartier/ville ; addressdetails=1 pour lire le champ address
    q.addQueryItem(QStringLiteral("zoom"), QStringLiteral("12"));
    q.addQueryItem(QStringLiteral("addressdetails"), QStringLiteral("1"));
    url.setQuery(q);

    QNetworkRequest req(url);
    // Nominatim exige un User-Agent identifiable (sinon HTTP 403)
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("utitibike/") + QStringLiteral(UTITIBIKE_VERSION)
                  + QStringLiteral(" (Ubuntu Touch)"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam.get(req);

    // Timeout 10 s
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(10000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReverseReply(reply);
    });
}

void SearchClient::onReverseReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "SearchClient reverse error:" << reply->error() << reply->errorString();
        emit reverseFailed();
        return;
    }

    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus != 200) {
        qWarning() << "SearchClient reverse HTTP" << httpStatus << body.left(200);
        emit reverseFailed();
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "SearchClient reverse JSON parse error:" << parseErr.errorString();
        emit reverseFailed();
        return;
    }

    // address : on prend le premier champ de lieu disponible, du plus
    // précis (quartier) au plus général (département/région)
    const QJsonObject address = doc.object().value(QStringLiteral("address")).toObject();
    const char *keys[] = { "suburb", "neighbourhood", "quarter", "hamlet",
                           "city", "town", "village", "municipality",
                           "county", "state_district", "state" };
    QString place;
    for (const char *k : keys) {
        const QString v = address.value(QLatin1String(k)).toString().trimmed();
        if (!v.isEmpty()) {
            place = v;
            break;
        }
    }
    emit reverseFinished(place);
}
