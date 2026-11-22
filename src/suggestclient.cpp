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
#include <QDateTime>
#include <QDebug>

// v0.99.1: l'instance principale overpass-api.de est souvent saturee
// (~35 s pour une requete, parfois timeout a 40 s). dresel.systems (Europe,
// donnees Geofabrik) repond en ~3 s avec les memes resultats. Fallback sur
// les instances globales en cas d'echec.
static const char *kOverpassMirrors[] = {
    "https://ovp.dresel.systems/api/interpreter",
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
};
static const int kMirrorCount = 4;
static const int kRadiusM = 100000;  // demande Mathias : 100 km pour trouver de vraies véloroutes
static const qint64 kCacheTtlMs = 10 * 60 * 1000;  // 10 min

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
                          void (SuggestClient::*onError)(const QString &),
                          int mirror)
{
    if (mirror >= kMirrorCount) {
        qWarning() << "suggest: tous les miroirs Overpass ont echoue";
        (this->*onError)(tr("Tous les serveurs de suggestions sont indisponibles."));
        return;
    }
    QUrl url(QString::fromLatin1(kOverpassMirrors[mirror]));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("data"), overpass);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("utitibike/UT WheelTrek (Ubuntu Touch)"));
    QNetworkReply *reply = m_nam.get(req);
    // v1.0: un seul numéro par requête LOGIQUE — les miroirs de secours
    // réutilisent le même seq. Avant, chaque miroir incrémentait le compteur
    // et pouvait invalider une requête plus récente (busy bloqué).
    const quint64 seq = m_querySeq;
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, seq, onSuccess, onError, mirror, overpass, timeoutMs]() {
        reply->deleteLater();
        if (seq != m_querySeq)
            return;  // une reponse plus recente a pris le dessus
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // v1.0: exiger HTTP 200 — un 404/429 ne positionne pas toujours
        // reply->error() en Qt5 et son corps HTML passait comme "réponse".
        if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
            qWarning() << "suggest: mirror" << mirror << "failed (HTTP" << httpStatus
                       << ")" << reply->errorString() << "- trying next";
            query(overpass, timeoutMs, onSuccess, onError, mirror + 1);
            return;
        }
        (this->*onSuccess)(reply->readAll());
    });
}

QString SuggestClient::cacheKey() const
{
    // Arrondi à ~1 km : deux recherches proches partagent le même résultat.
    return QStringLiteral("%1|%2|%3|%4")
            .arg(qRound(m_lat * 100)).arg(qRound(m_lon * 100))
            .arg(m_mode).arg(m_radiusM);
}

void SuggestClient::suggest(double lat, double lon, const QString &mode, int radiusM)
{
    // L'UI passe un rayon proportionnel à la durée max choisie ; 0 = défaut.
    ++m_querySeq;  // nouvelle requête logique
    m_radiusM = (radiusM > 0) ? radiusM : kRadiusM;
    m_lat = lat;
    m_lon = lon;
    m_mode = (mode == QLatin1String("foot")) ? QStringLiteral("foot")
                                             : QStringLiteral("bike");
    // v0.99.1: cache 10 min — rouvrir le panneau ou revenir à un filtre déjà
    // affiché ne relance plus une requête de ~28 s.
    if (m_cacheKey == cacheKey()
            && QDateTime::currentMSecsSinceEpoch() - m_cacheAtMs < kCacheTtlMs) {
        setBusy(false);
        emit suggestionsReady(m_cacheItems);
        return;
    }
    m_names.clear();
    setBusy(true);
    // v0.99.1: relations ET ways nommees — beaucoup de sentiers locaux ne
    // sont pas des relations OSM (23 ways supplementaires autour de Toulouse).
    const QString routeFilter = m_mode == QLatin1String("foot")
            ? QStringLiteral("[\"route\"~\"^(hiking|foot)$\"]")
            : QStringLiteral("[\"route\"~\"^(bicycle|mtb)$\"]");
    const QString q = QStringLiteral("[out:json][timeout:60];"
                                     "(relation[\"type\"=\"route\"]%1[\"name\"]"
                                     "(around:%2,%3,%4);"
                                     "relation[\"type\"=\"route\"]%1[\"ref\"]"
                                     "(around:%2,%3,%4);"
                                     "way%1[\"name\"](around:%2,%3,%4););"
                                     "out center tags;")
            .arg(routeFilter)
            .arg(m_radiusM)
            .arg(lat, 0, 'f', 6)
            .arg(lon, 0, 'f', 6);
    // Un rayon de 100 km renvoie ~200 relations et met ~28 s au miroir
    // rapide ; le timeout client doit suivre (40 s -> 60 s à 100 km).
    const int timeoutMs = 40000 + m_radiusM / 5;
    qWarning() << "suggest query:" << q << "timeout" << timeoutMs;
    query(q, timeoutMs, &SuggestClient::onSuggestReply, &SuggestClient::onSuggestError);
}

void SuggestClient::refresh()
{
    m_cacheKey.clear();
    m_cacheItems.clear();
    m_cacheAtMs = 0;
    suggest(m_lat, m_lon, m_mode, m_radiusM);
}

void SuggestClient::cancel()
{
    // Invalide les réponses en vol (suggest + trail) via le compteur de
    // séquence déjà utilisé par query().
    ++m_querySeq;
    setBusy(false);
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
    // rank : 0 = réseau national (ncn, longues véloroutes), 1 = régional,
    // 2 = local, 3 = inconnu. On trie par pertinence avant distance : sinon
    // la liste ne montrait que les petits circuits les plus proches.
    struct Item { QVariantMap map; double distM; int rank; };
    QList<Item> items;
    const QJsonArray elements = doc.object().value(QStringLiteral("elements")).toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject el = v.toObject();
        const QString osmType = el.value(QStringLiteral("type")).toString();
        // v0.99.1: accepter aussi les ways nommees (sentiers locaux).
        if (osmType != QLatin1String("relation") && osmType != QLatin1String("way"))
            continue;
        const QJsonObject tags = el.value(QStringLiteral("tags")).toObject();
        QString name = tags.value(QStringLiteral("name")).toString();
        // Certaines véloroutes n'ont que leur numéro (ex. V85) : on
        // l'affiche plutôt que de jeter le tracé.
        if (name.isEmpty())
            name = tags.value(QStringLiteral("ref")).toString();
        if (name.isEmpty())
            continue;
        const QJsonObject center = el.value(QStringLiteral("center")).toObject();
        const double lat = center.value(QStringLiteral("lat")).toDouble();
        const double lon = center.value(QStringLiteral("lon")).toDouble();
        if (!qIsFinite(lat) || !qIsFinite(lon))
            continue;
        const qint64 id = el.value(QStringLiteral("id")).toVariant().toLongLong();
        m_names.insert(id, name);
        m_types.insert(id, osmType);
        QVariantMap m;
        m.insert(QStringLiteral("id"), QVariant::fromValue(id));
        m.insert(QStringLiteral("type"), osmType);
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("route"), tags.value(QStringLiteral("route")).toString());
        const QString network = tags.value(QStringLiteral("network")).toString();
        m.insert(QStringLiteral("network"), network);
        m.insert(QStringLiteral("lat"), lat);
        m.insert(QStringLiteral("lon"), lon);
        const double distM = haversine(m_lat, m_lon, lat, lon);
        m.insert(QStringLiteral("distM"), distM);
        // Temps estimé du tracé : tag OSM « distance » (km) de la relation,
        // à la vitesse du mode (marche ~5 km/h, vélo ~18 km/h). -1 si inconnu.
        double estH = -1.0;
        double routeDistanceM = -1.0;
        const double speedKmh = (m_mode == QLatin1String("foot")) ? 5.0 : 18.0;
        const QString distTag = tags.value(QStringLiteral("distance")).toString();
        QRegExp rx(QStringLiteral("[0-9]+([.,][0-9]+)?"));
        if (rx.indexIn(distTag) >= 0) {
            const double km = rx.cap(0).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
            if (km > 0) {
                estH = km / speedKmh;
                routeDistanceM = km * 1000.0;
            }
        } else {
            // v0.99.1: ~34 tracés sur 44 n'ont PAS de tag « distance » —
            // sans estimation ils disparaissaient dès qu'un filtre de durée
            // était actif. Ordre de grandeur d'après le réseau OSM.
            double guessKm = -1.0;
            if (network == QLatin1String("ncn"))      guessKm = 80.0;
            else if (network == QLatin1String("rcn")) guessKm = 30.0;
            else if (network == QLatin1String("lcn")) guessKm = 12.0;
            if (guessKm > 0)
                estH = guessKm / speedKmh;
        }
        m.insert(QStringLiteral("estH"), estH);
        m.insert(QStringLiteral("routeDistanceM"), routeDistanceM);
        const int rank = (network == QLatin1String("ncn")) ? 0
                       : (network == QLatin1String("rcn")) ? 1
                       : (network == QLatin1String("lcn")) ? 2 : 3;
        items.append({m, distM, rank});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.distM < b.distM;
    });
    QVariantList out;
    const int maxItems = qMin<int>(items.size(), 40);
    for (int i = 0; i < maxItems; ++i)
        out.append(items.at(i).map);
    // v1.0: un corps {"remark":"rate limited"} n'a pas d'éléments — ne pas
    // le mettre en cache 10 min et ne pas annoncer une liste vide.
    if (!doc.object().contains(QStringLiteral("elements"))) {
        emit suggestionsFailed(tr("Réponse illisible du serveur de suggestions."));
        return;
    }
    m_cacheKey = cacheKey();
    m_cacheItems = out;
    m_cacheAtMs = QDateTime::currentMSecsSinceEpoch();
    qWarning() << "suggest:" << out.size() << "suggestions (cached)";
    emit suggestionsReady(out);
}

void SuggestClient::fetchTrail(qint64 id, const QString &type)
{
    ++m_querySeq;  // nouvelle requête logique
    m_pendingId = id;
    setBusy(true);
    // v0.99.1: une suggestion peut etre une way (pas de membres) — dans ce
    // cas on recupere directement sa geometrie.
    const QString osmType = m_types.value(id, type);
    const QString q = osmType == QLatin1String("way")
            ? QStringLiteral("[out:json][timeout:60];way(%1);out geom;").arg(id)
            : QStringLiteral("[out:json][timeout:60];relation(%1);way(r);out geom;").arg(id);
    qWarning() << "fetchTrail" << osmType << id;
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
