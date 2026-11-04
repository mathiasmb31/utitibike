
#include "brouterclient.h"
#include "geo.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QtMath>
#include <QTimer>
#include <QDebug>
#include <limits>

namespace {

// Garmin Connect refuse les GPX sans horodatages (« fichier invalide »), alors
// que BRouter n'en produit pas. On ajoute un <time> à chaque <trkpt>, espacé
// selon la distance (≈ 20 km/h), à partir de l'heure actuelle en UTC.
QString addTimestampsToGpx(const QString &gpx)
{
    if (gpx.contains(QLatin1String("<time")))
        return gpx;

    QString out;
    out.reserve(gpx.size() + 4096);
    QXmlStreamReader xml(gpx);
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);   // GPX lisible (un élément par ligne)
    QDateTime t = QDateTime::currentDateTimeUtc();
    double prevLat = 0.0;
    double prevLon = 0.0;
    bool havePrev = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError())
            return gpx; // XML illisible : on exporte le fichier original
        if (xml.isStartDocument())
            continue;
        if (xml.isStartElement()) {
            w.writeStartElement(xml.name().toString());
            const QXmlStreamNamespaceDeclarations nsDecls = xml.namespaceDeclarations();
            for (const QXmlStreamNamespaceDeclaration &ns : nsDecls) {
                if (ns.prefix().isEmpty())
                    w.writeDefaultNamespace(ns.namespaceUri().toString());
                else
                    w.writeNamespace(ns.namespaceUri().toString(), ns.prefix().toString());
            }
            const QXmlStreamAttributes attrs = xml.attributes();
            for (const QXmlStreamAttribute &a : attrs) {
                // name() renvoie le nom local : reconstituer le nom qualifié
                // (p. ex. xsi:schemaLocation) pour ne rien perdre.
                const QStringRef prefix = a.prefix();
                const QString qname = prefix.isEmpty()
                        ? a.name().toString()
                        : prefix.toString() + QLatin1Char(':') + a.name().toString();
                w.writeAttribute(qname, a.value().toString());
            }
            if (xml.name() == QLatin1String("trkpt")) {
                const double lat = xml.attributes().value(QLatin1String("lat")).toDouble();
                const double lon = xml.attributes().value(QLatin1String("lon")).toDouble();
                if (havePrev) {
                    const double d = haversine(prevLat, prevLon, lat, lon);
                    t = t.addSecs(qMax(1, qRound(d / 5.5))); // ~20 km/h
                }
                w.writeTextElement(QStringLiteral("time"), t.toString(Qt::ISODateWithMs));
                prevLat = lat;
                prevLon = lon;
                havePrev = true;
            }
        } else if (xml.isEndElement()) {
            w.writeEndElement();
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            w.writeCharacters(xml.text().toString());
        }
    }
    out.prepend(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    return out;
}

} // namespace

BrouterClient::BrouterClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

bool BrouterClient::busy() const
{
    return m_busy;
}

QString BrouterClient::profile() const
{
    return m_profile;
}

void BrouterClient::setProfile(const QString &p)
{
    if (m_profile == p)
        return;
    m_profile = p;
    emit profileChanged();
}

QString BrouterClient::lastGpx() const
{
    return m_lastGpx;
}

int BrouterClient::uphillCost() const
{
    return m_uphillCost;
}

void BrouterClient::setUphillCost(int v)
{
    if (m_uphillCost == v)
        return;
    m_uphillCost = v;
    emit uphillCostChanged();
}

void BrouterClient::setAvoidUnpaved(bool v)
{
    if (m_avoidUnpaved == v)
        return;
    m_avoidUnpaved = v;
    emit avoidUnpavedChanged();
}

void BrouterClient::setLoadingFromHistory(bool v)
{
    if (m_loadingFromHistory == v)
        return;
    m_loadingFromHistory = v;
    emit loadingFromHistoryChanged();
}

void BrouterClient::setBusy(bool v)
{
    if (m_busy == v)
        return;
    m_busy = v;
    emit busyChanged();
}

void BrouterClient::requestRoute(const QVariantList &waypoints)
{
    qWarning() << "brouter requestRoute:" << waypoints.size() << "waypoints";
    QStringList coords;
    for (int i = 0; i < waypoints.size(); ++i) {
        const QVariantMap m = waypoints.at(i).toMap();
        const double lat = m.value(QStringLiteral("lat")).toDouble();
        const double lon = m.value(QStringLiteral("lon")).toDouble();
        qWarning() << "brouter waypoint" << i + 1 << ":" << lat << "," << lon;
        // Défense : un waypoint invalide (NaN, hors limites, ou 0,0) ferait
        // échouer le calcul BRouter ou produirait un itinéraire absurde
        // passant par le Golfe de Guinée.
        if (!qIsFinite(lat) || !qIsFinite(lon)
                || lat < -90.0 || lat > 90.0
                || lon < -180.0 || lon > 180.0
                || (lat == 0.0 && lon == 0.0)) {
            const QString msg = tr("Point %1 invalide (%2, %3) — supprimez-le et réessayez.")
                             .arg(i + 1).arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5);
            qWarning() << "brouter FAILED:" << msg;
            emit routeFailed(msg);
            return;
        }
        coords << QString::number(lon, 'f', 6) + QLatin1Char(',')
               + QString::number(lat, 'f', 6);
    }
    if (coords.size() < 2) {
        const QString msg = tr("Ajoutez au moins deux points (appui long sur la carte).");
        qWarning() << "brouter FAILED:" << msg;
        emit routeFailed(msg);
        return;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    setBusy(true);

    QUrl url(QStringLiteral("https://brouter.de/brouter"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("lonlats"), coords.join(QLatin1Char('|')));
    q.addQueryItem(QStringLiteral("profile"),
                   m_profile.isEmpty() ? QStringLiteral("trekking") : m_profile);
    q.addQueryItem(QStringLiteral("alternativeidx"), QStringLiteral("0"));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("gpx"));
    // Turn-by-turn instructions (texte lisible dans les <desc> des <rtept>)
    q.addQueryItem(QStringLiteral("turnInstructionFormat"), QStringLiteral("turn"));
    // Pass uphill cost so the user can prefer flat or hilly routes.
    q.addQueryItem(QStringLiteral("uphillcost"), QString::number(m_uphillCost));
    // Éviter les chemins non goudronnés : unpavedPenalty est un multiplicateur
    // de pénalité défini dans le profil trekking de BRouter (les profils qui
    // ne le définissent pas l'ignorent silencieusement). 10 = forte pénalité,
    // les surfaces non goudronnées deviennent quasiment inaccessibles.
    if (m_avoidUnpaved)
        q.addQueryItem(QStringLiteral("unpavedPenalty"), QStringLiteral("10"));
    url.setQuery(q);
    qWarning() << "brouter URL:" << url.toString();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("utitibike/" UTITIBIKE_VERSION " (Ubuntu Touch)"));
    m_reply = m_nam->get(req);

    QTimer::singleShot(90000, m_reply, [this]() {
        if (m_reply && m_reply->isRunning()) {
            qWarning() << "brouter TIMEOUT after 90s — aborting";
            m_reply->abort();
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        setBusy(false);
        const int httpStatus = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qWarning() << "brouter reply finished: HTTP" << httpStatus
                   << "error" << r->error() << "-" << r->errorString();
        if (r->error() != QNetworkReply::NoError) {
            const QString msg = (r->error() == QNetworkReply::OperationCanceledError)
                  ? tr("Délai dépassé — réessayez.")
                  : tr("Réseau : %1").arg(r->errorString());
            qWarning() << "brouter FAILED:" << msg;
            emit routeFailed(msg);
            return;
        }
        const QByteArray body = r->readAll();
        qWarning() << "brouter response:" << body.size() << "bytes";
        parseGpx(QString::fromUtf8(body));
    });
}

void BrouterClient::parseGpx(const QString &gpxText)
{
    qWarning() << "brouter parseGpx:" << gpxText.size() << "chars";
    const QString trimmed = gpxText.trimmed();
    if (!trimmed.startsWith(QLatin1Char('<'))) {
        const QString msg = trimmed.isEmpty() ? tr("Réponse vide de BRouter.")
                                               : trimmed.left(200);
        qWarning() << "brouter FAILED: non-XML response:" << msg;
        emit routeFailed(msg);
        return;
    }

    QXmlStreamReader xml(gpxText);
    QVariantList points;
    QVariantList turnPoints;
    double dist = 0.0;
    double ascent = 0.0;
    double prevLat = 0.0;
    double prevLon = 0.0;
    double prevEle = 0.0;
    bool havePrev = false;
    bool haveEle = false;
    // BRouter peut couper un itinéraire en plusieurs <trkseg> (ferry, tunnel,
    // zone sans données). On insère un point NaN entre les segments pour que
    // visiblePath coupe proprement la ligne (un NaN est filtré par isfinite).
    bool inFirstTrkSeg = true;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError()) {
            const QString msg = tr("GPX illisible.");
            qWarning() << "brouter FAILED: XML parse error:" << xml.errorString()
                       << "at line" << xml.lineNumber();
            emit routeFailed(msg);
            return;
        }
        // Détecter le début d'un nouveau <trkseg> (après le premier)
        if (xml.isStartElement() && xml.name() == QLatin1String("trkseg")) {
            if (!inFirstTrkSeg && !points.isEmpty()) {
                // Marqueur de séparation : NaN sera filtré par visiblePath
                QVariantMap sep;
                sep.insert(QStringLiteral("lat"),
                           std::numeric_limits<double>::quiet_NaN());
                sep.insert(QStringLiteral("lon"),
                           std::numeric_limits<double>::quiet_NaN());
                points.append(sep);
                havePrev = false;  // reset : pas de distance à travers le trou
            }
            inFirstTrkSeg = false;
            continue;
        }
        // Capture les <rtept> (turn instructions) avant le <trk>
        if (xml.isStartElement() && xml.name() == QLatin1String("rtept")) {
            const double lat = xml.attributes().value(QLatin1String("lat")).toDouble();
            const double lon = xml.attributes().value(QLatin1String("lon")).toDouble();
            QString desc;
            QString cmt;
            while (!(xml.isEndElement() && xml.name() == QLatin1String("rtept"))) {
                xml.readNext();
                if (xml.hasError()) break;
                if (xml.isStartElement() && xml.name() == QLatin1String("desc"))
                    desc = xml.readElementText();
                if (xml.isStartElement() && xml.name() == QLatin1String("cmt"))
                    cmt = xml.readElementText();
            }
            if (xml.hasError()) {
                const QString msg = tr("GPX illisible.");
                qWarning() << "brouter FAILED: XML error in rtept:" << xml.errorString();
                emit routeFailed(msg);
                return;
            }
            if (!desc.isEmpty()) {
                QVariantMap tp;
                tp.insert(QStringLiteral("lat"), lat);
                tp.insert(QStringLiteral("lon"), lon);
                tp.insert(QStringLiteral("desc"), desc);
                // BRouter met parfois la distance cumulée dans <cmt>
                bool cmtOk = false;
                double cmtVal = cmt.toDouble(&cmtOk);
                tp.insert(QStringLiteral("distance"), cmtOk ? cmtVal : 0.0);
                turnPoints.append(tp);
            }
            continue;
        }
        if (xml.isStartElement() && xml.name() == QLatin1String("trkpt")) {
            const double lat = xml.attributes().value(QLatin1String("lat")).toDouble();
            const double lon = xml.attributes().value(QLatin1String("lon")).toDouble();
            // Rejeter les coordonnées invalides (NaN, hors limites) : un point
            // NaN passerait tous les filtres de visiblePath (NaN < x est faux)
            // et produirait une ligne artefact vers un point fantôme invisible.
            if (!qIsFinite(lat) || !qIsFinite(lon)
                    || lat < -90.0 || lat > 90.0
                    || lon < -180.0 || lon > 180.0)
                continue;
            double ele = 0.0;
            bool eleOk = false;
            while (!(xml.isEndElement() && xml.name() == QLatin1String("trkpt"))) {
                xml.readNext();
                if (xml.hasError())
                    break;
                if (xml.isStartElement() && xml.name() == QLatin1String("ele")) {
                    ele = xml.readElementText().toDouble();
                    eleOk = true;
                }
            }
            if (xml.hasError()) {
                const QString msg = tr("GPX illisible.");
                qWarning() << "brouter FAILED: XML error in trkpt:" << xml.errorString();
                emit routeFailed(msg);
                return;
            }
            QVariantMap p;
            p.insert(QStringLiteral("lat"), lat);
            p.insert(QStringLiteral("lon"), lon);
            if (eleOk)
                p.insert(QStringLiteral("ele"), ele);
            points.append(p);
            if (havePrev)
                dist += haversine(prevLat, prevLon, lat, lon);
            if (eleOk && haveEle && ele > prevEle)
                ascent += ele - prevEle;
            prevLat = lat;
            prevLon = lon;
            // D+ entre altitudes valides uniquement : prevEle ne garde que
            // la dernière altitude connue (un point sans <ele> ne doit pas
            // faire croire à une descente jusqu'à 0 m).
            if (eleOk) {
                prevEle = ele;
                haveEle = true;
            }
            havePrev = true;
        }
    }

    if (points.size() < 2) {
        const QString msg = tr("Aucun itinéraire dans la réponse BRouter.");
        qWarning() << "brouter FAILED:" << msg << "(only" << points.size() << "points)";
        emit routeFailed(msg);
        return;
    }

    qWarning() << "brouter OK:" << points.size() << "points,"
               << qRound(dist) << "m," << qRound(ascent) << "m D+,"
               << turnPoints.size() << "turns";
    m_lastGpx = gpxText;
    m_routePoints = points;
    m_turnPoints = turnPoints;
    emit lastGpxChanged();
    emit routeReady(points, dist, ascent);
}

QString BrouterClient::exportGpx(const QString &gpx) const
{
    if (gpx.trimmed().isEmpty())
        return QString();
    // On écrit dans le TMPDIR confiné : créé par le framework au lancement,
    // il existe toujours. L'app ne peut pas faire de mkdir elle-même
    // (AppArmor 4 exige le droit "c" que le framework ne génère pas).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(dir);
    // Nom unique à la seconde, avec suffixe si deux exports dans la même seconde
    const QString base = QStringLiteral("itineraire-")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name = base + QStringLiteral(".gpx");
    QString path = dir + QLatin1Char('/') + name;
    for (int n = 1; QFile::exists(path); ++n) {
        name = base + QStringLiteral("-%1.gpx").arg(n);
        path = dir + QLatin1Char('/') + name;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(addTimestampsToGpx(gpx).toUtf8());
    if (!f.commit())
        return QString();
    return QUrl::fromLocalFile(path).toString();
}

void BrouterClient::loadGpx(const QString &fileUrl)
{
    const QUrl url(fileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit routeFailed(tr("Impossible d'ouvrir le fichier GPX."));
        return;
    }
    const QString gpx = QString::fromUtf8(f.readAll());
    f.close();
    parseGpx(gpx);
}

void BrouterClient::loadGpxData(const QString &gpxContent)
{
    parseGpx(gpxContent);
}

void BrouterClient::startNavigation()
{
    if (m_navigation || m_routePoints.size() < 2)
        return;
    m_navigation = true;
    emit navigationChanged();
    // Première instruction
    if (!m_turnPoints.isEmpty()) {
        const QVariantMap tp = m_turnPoints.first().toMap();
        m_nextInstruction = tp.value(QStringLiteral("desc")).toString();
        m_nextInstructionDistance = 0.0;
        emit nextInstructionChanged();
    } else {
        // GPX importé sans <rtept> : pas d'instructions de virage. Guidage
        // minimal « Arrivée » + distance restante, mise à jour à chaque fix.
        m_nextInstruction = tr("Arrivée");
        m_nextInstructionDistance = 0.0;
        emit nextInstructionChanged();
    }
}

void BrouterClient::stopNavigation()
{
    if (!m_navigation)
        return;
    m_navigation = false;
    m_nextInstruction.clear();
    m_nextInstructionDistance = 0.0;
    emit navigationChanged();
    emit nextInstructionChanged();
}

void BrouterClient::updatePosition(double lat, double lon)
{
    if (!m_navigation || m_routePoints.size() < 2)
        return;

    // Trouve l'index du point de route le plus proche de notre position
    int closestIdx = 0;
    double closestDist = haversine(lat, lon,
        m_routePoints[0].toMap().value(QStringLiteral("lat")).toDouble(),
        m_routePoints[0].toMap().value(QStringLiteral("lon")).toDouble());
    for (int i = 1; i < m_routePoints.size(); ++i) {
        const QVariantMap p = m_routePoints[i].toMap();
        const double d = haversine(lat, lon,
            p.value(QStringLiteral("lat")).toDouble(),
            p.value(QStringLiteral("lon")).toDouble());
        if (d < closestDist) {
            closestDist = d;
            closestIdx = i;
        }
    }

    // Pas d'instructions de virage (GPX importé sans <rtept>) : guidage
    // minimal, la distance jusqu'à l'arrivée est recalculée à chaque fix.
    if (m_turnPoints.isEmpty()) {
        const QVariantMap last = m_routePoints.last().toMap();
        const double dArrival = haversine(lat, lon,
            last.value(QStringLiteral("lat")).toDouble(),
            last.value(QStringLiteral("lon")).toDouble());
        if (m_nextInstruction.isEmpty()
                || qAbs(dArrival - m_nextInstructionDistance) > 5.0) {
            m_nextInstruction = tr("Arrivée");
            m_nextInstructionDistance = dArrival;
            emit nextInstructionChanged();
        }
        if (dArrival <= 20.0) {
            m_nextInstructionDistance = 0.0;
            emit nextInstructionChanged();
            emit routeFinished();
            stopNavigation();
        }
        return;
    }

    // Trouve le prochain point de virage après notre position
    for (int i = 0; i < m_turnPoints.size(); ++i) {
        const QVariantMap tp = m_turnPoints[i].toMap();
        const double tLat = tp.value(QStringLiteral("lat")).toDouble();
        const double tLon = tp.value(QStringLiteral("lon")).toDouble();
        // Distance jusqu'au virage
        const double distToTurn = haversine(lat, lon, tLat, tLon);
        // Distance entre le virage et le point de route le plus proche
        // (pour savoir si on a déjà passé ce virage)
        const QVariantMap cp = m_routePoints[closestIdx].toMap();
        const double turnToClosest = haversine(tLat, tLon,
            cp.value(QStringLiteral("lat")).toDouble(),
            cp.value(QStringLiteral("lon")).toDouble());
        // Si le virage est devant nous (plus loin sur la route)
        if (turnToClosest > 5.0) {
            const QString instr = tp.value(QStringLiteral("desc")).toString();
            if (instr != m_nextInstruction || qAbs(distToTurn - m_nextInstructionDistance) > 5.0) {
                m_nextInstruction = instr;
                m_nextInstructionDistance = distToTurn;
                // Nouveau virage : reset les alertes
                m_alert200 = false;
                m_alert100 = false;
                m_alert50 = false;
                emit nextInstructionChanged();
            }
            // Alertes de proximité (une seule fois par seuil)
            if (!m_alert200 && distToTurn <= 200.0) {
                m_alert200 = true;
                emit turnAlert(0);
            }
            if (!m_alert100 && distToTurn <= 100.0) {
                m_alert100 = true;
                emit turnAlert(1);
            }
            if (!m_alert50 && distToTurn <= 50.0) {
                m_alert50 = true;
                emit turnAlert(2);
            }
            return;
        }
    }

    // Plus de virage restant : on est arrivé
    if (!m_nextInstruction.isEmpty()) {
        m_nextInstruction = tr("Arrivée");
        m_nextInstructionDistance = 0.0;
        emit nextInstructionChanged();
        emit routeFinished();
        stopNavigation();
    }
}
