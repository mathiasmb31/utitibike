#include "suggestclient.h"
#include "geo.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegExp>
#include <QTimer>
#include <QDebug>

static const char kOverpassUrl[] = "https://overpass-api.de/api/interpreter";
static const int kRadiusM = 5000;

SuggestClient::SuggestClient(QObject *parent)
    : QObject(parent)
{
}

void SuggestClient::setBusy(bool v)
{
    if (m_busy == v)
        return;
    m_busy = v;
    emit busyChanged();
}

void SuggestClient::query(const QString &overpass, int timeoutMs,
                          void (SuggestClient::*onSuccess)(const QByteArray &),
                          void (SuggestClient::*onError)(const QString &))
{
    QUrl url(QString::fromLatin1(kOverpassUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("data"), overpass);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("utitibike/UT WheelTrek (Ubuntu Touch)"));
    QNetworkReply *reply = m_nam.get(req);
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        reply->deleteLater();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "suggest: network error" << reply->error() << reply->errorString()
                       << "HTTP" << httpStatus;
            (this->*onError)(reply->error() == QNetworkReply::OperationCanceledError
                             ? tr("Délai dépassé — réessayez.")
                             : tr("Réseau : %1").arg(reply->errorString()));
            return;
        }
        (this->*onSuccess)(reply->readAll());
    });
}

void SuggestClient::suggest(double lat, double lon, const QString &mode)
{
    m_lat = lat;
    m_lon = lon;
    m_mode = (mode == QLatin1String("foot")) ? QStringLiteral("foot")
                                             : QStringLiteral("bike");
    m_names.clear();
    setBusy(true);
    // Relations de randonnée (foot) ou vélo (bicycle/mtb), nommées, dans le rayon
    const QString routeFilter = m_mode == QLatin1String("foot")
            ? QStringLiteral("[\"route\"~\"^(hiking|foot)$\"]")
            : QStringLiteral("[\"route\"~\"^(bicycle|mtb)$\"]");
    const QString q = QStringLiteral("[out:json][timeout:25];"
                                     "relation[\"type\"=\"route\"]%1[\"name\"]"
                                     "(around:%2,%3,%4);out center tags;")
            .arg(routeFilter)
            .arg(kRadiusM)
            .arg(lat, 0, 'f', 6)
            .arg(lon, 0, 'f', 6);
    qWarning() << "suggest query:" << q;
    query(q, 40000, &SuggestClient::onSuggestReply, &SuggestClient::onSuggestError);
}

void SuggestClient::onSuggestError(const QString &msg)
{
    setBusy(false);
    emit suggestionsFailed(msg);
}

void SuggestClient::onSuggestReply(const QByteArray &body)
{
    setBusy(false);
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "suggest: bad JSON" << err.errorString();
        emit suggestionsFailed(tr("Réponse illisible du serveur de suggestions."));
        return;
    }
    struct Item { QVariantMap map; double distM; };
    QList<Item> items;
    const QJsonArray elements = doc.object().value(QStringLiteral("elements")).toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject el = v.toObject();
        if (el.value(QStringLiteral("type")).toString() != QLatin1String("relation"))
            continue;
        const QJsonObject tags = el.value(QStringLiteral("tags")).toObject();
        const QString name = tags.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QJsonObject center = el.value(QStringLiteral("center")).toObject();
        const double lat = center.value(QStringLiteral("lat")).toDouble();
        const double lon = center.value(QStringLiteral("lon")).toDouble();
        if (!qIsFinite(lat) || !qIsFinite(lon))
            continue;
        const qint64 id = el.value(QStringLiteral("id")).toVariant().toLongLong();
        m_names.insert(id, name);
        QVariantMap m;
        m.insert(QStringLiteral("id"), QVariant::fromValue(id));
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("route"), tags.value(QStringLiteral("route")).toString());
        m.insert(QStringLiteral("lat"), lat);
        m.insert(QStringLiteral("lon"), lon);
        const double distM = haversine(m_lat, m_lon, lat, lon);
        m.insert(QStringLiteral("distM"), distM);
        // Temps estimé du tracé : tag OSM « distance » (km) de la relation,
        // à la vitesse du mode (marche ~5 km/h, vélo ~18 km/h). -1 si inconnu.
        double estH = -1.0;
        const QString distTag = tags.value(QStringLiteral("distance")).toString();
        QRegExp rx(QStringLiteral("[0-9]+([.,][0-9]+)?"));
        if (rx.indexIn(distTag) >= 0) {
            const double km = rx.cap(0).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
            const double speedKmh = (m_mode == QLatin1String("foot")) ? 5.0 : 18.0;
            if (km > 0)
                estH = km / speedKmh;
        }
        m.insert(QStringLiteral("estH"), estH);
        items.append({m, distM});
    }
    std::sort(items.begin(), items.end(),
              [](const Item &a, const Item &b) { return a.distM < b.distM; });
    QVariantList out;
    const int maxItems = qMin<int>(items.size(), 25);
    for (int i = 0; i < maxItems; ++i)
        out.append(items.at(i).map);
    qWarning() << "suggest:" << out.size() << "suggestions";
    emit suggestionsReady(out);
}

void SuggestClient::fetchTrail(qint64 relationId)
{
    m_pendingId = relationId;
    setBusy(true);
    const QString q = QStringLiteral("[out:json][timeout:60];"
                                     "relation(%1);way(r);out geom;")
            .arg(relationId);
    qWarning() << "fetchTrail relation" << relationId;
    query(q, 75000, &SuggestClient::onTrailReply, &SuggestClient::onTrailError);
}

void SuggestClient::onTrailError(const QString &msg)
{
    setBusy(false);
    emit trailFailed(msg);
}

static QString xmlEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
       .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
       .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
       .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

// Construit une polyligne unique à partir des ways membres de la relation :
// chaînage glouton (chaque way est accolé à l'extrémité la plus proche de
// la fin du tracé courant, en l'inversant si besoin).
QString SuggestClient::trailGpx(const QJsonDocument &doc) const
{
    struct Way {
        QVector<QPair<double, double>> pts; // (lat, lon)
    };
    QList<Way> ways;
    const QJsonArray elements = doc.object().value(QStringLiteral("elements")).toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject el = v.toObject();
        if (el.value(QStringLiteral("type")).toString() != QLatin1String("way"))
            continue;
        Way w;
        const QJsonArray geom = el.value(QStringLiteral("geometry")).toArray();
        for (const QJsonValue &g : geom) {
            const QJsonObject p = g.toObject();
            w.pts.append({p.value(QStringLiteral("lat")).toDouble(),
                          p.value(QStringLiteral("lon")).toDouble()});
        }
        if (w.pts.size() >= 2)
            ways.append(w);
    }
    if (ways.isEmpty())
        return QString();

    QVector<QPair<double, double>> line;
    QVector<bool> used(ways.size(), false);
    // Point de départ : extrémité la plus proche de la position courante
    int start = 0;
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < ways.size(); ++i) {
        for (int end = 0; end < 2; ++end) {
            const auto p = end == 0 ? ways[i].pts.first() : ways[i].pts.last();
            const double d = haversine(m_lat, m_lon, p.first, p.second);
            if (d < best) { best = d; start = i; }
        }
    }
    used[start] = true;
    line = ways[start].pts;
    while (true) {
        const auto curEnd = line.last();
        int next = -1;
        bool flip = false;
        double bestD = 500.0; // écart max pour souder deux ways (m)
        for (int i = 0; i < ways.size(); ++i) {
            if (used[i])
                continue;
            const double dFirst = haversine(curEnd.first, curEnd.second,
                                            ways[i].pts.first().first, ways[i].pts.first().second);
            const double dLast = haversine(curEnd.first, curEnd.second,
                                           ways[i].pts.last().first, ways[i].pts.last().second);
            if (dFirst <= dLast && dFirst < bestD) { bestD = dFirst; next = i; flip = false; }
            else if (dLast < dFirst && dLast < bestD) { bestD = dLast; next = i; flip = true; }
        }
        if (next < 0)
            break;
        used[next] = true;
        const Way &w = ways[next];
        if (flip) {
            for (int i = w.pts.size() - 1; i >= 0; --i)
                line.append(w.pts[i]);
        } else {
            for (int i = 0; i < w.pts.size(); ++i)
                line.append(w.pts[i]);
        }
    }

    QString gpx = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<gpx version=\"1.1\" creator=\"UT WheelTrek\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\">"
        "<trk><name>%1</name><trkseg>")
            .arg(xmlEscape(m_names.value(m_pendingId)));
    for (const auto &p : line)
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"%2\"/>")
                   .arg(p.first, 0, 'f', 6).arg(p.second, 0, 'f', 6);
    gpx += QStringLiteral("</trkseg></trk></gpx>");
    return gpx;
}

void SuggestClient::onTrailReply(const QByteArray &body)
{
    setBusy(false);
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit trailFailed(tr("Réponse illisible du serveur de suggestions."));
        return;
    }
    const QString gpx = trailGpx(doc);
    if (gpx.isEmpty()) {
        emit trailFailed(tr("Tracé introuvable ou sans géométrie."));
        return;
    }
    emit trailReady(gpx);
}
