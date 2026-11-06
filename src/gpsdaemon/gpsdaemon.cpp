#include "gpsdaemon.h"
#include "geo.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamWriter>
#include <QStorageInfo>
#include <QDebug>
#include <QtMath>
#include <QtDBus/QDBusVariant>
#include <QtDBus/QDBusPendingCall>
#include <algorithm>
#include <csignal>
#include <unistd.h>
#include <sys/prctl.h>

// Signaux UNIX : le daemon sauve et s'arrête proprement (SIGTERM/SIGINT =
// arrêt demandé ; SIGSEGV/SIGABRT/SIGBUS/SIGFPE = crash — on sauve quand même).
static GpsDaemon *g_daemon = nullptr;
static volatile sig_atomic_t g_inSignal = 0;
static void signalHandler(int sig) {
    // Garde contre la récursion (ex. segfault dans le handler lui-même)
    if (g_inSignal)
        _exit(128 + sig);
    g_inSignal = 1;
    if (g_daemon) {
        if (sig == SIGTERM || sig == SIGINT)
            g_daemon->setStopReason(QStringLiteral("control"));
        else
            g_daemon->setStopReason(QStringLiteral("crash"));
        QMetaObject::invokeMethod(g_daemon, "cleanup", Qt::QueuedConnection);
    }
}

GpsDaemon::GpsDaemon(QObject *parent)
    : QObject(parent)
{
    // Même applicationName que l'app principale pour partager appDataDir.
    m_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_controlFile = m_appDataDir + QStringLiteral("/recording.control");
    m_statsFile   = m_appDataDir + QStringLiteral("/recording.json");
    m_doneFile    = m_appDataDir + QStringLiteral("/recording.done");
    m_tracksDir   = m_appDataDir + QStringLiteral("/tracks");
    m_partFile    = m_tracksDir + QStringLiteral("/en-cours.part");
    m_manualStartFile = m_appDataDir + QStringLiteral("/manual-start.control");
    m_confFile    = m_appDataDir + QStringLiteral("/gpsd.conf");

    QDir().mkpath(m_tracksDir);

    // Beep : fichiers installés à côté de l'exécutable dans le click.
    m_beep.setSource(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/beep.wav")));
    m_beep.setVolume(1.0f);
    // Beep long d'alerte « action requise » (départ manuel proposé)
    m_beepLong.setSource(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/beep-long.wav")));
    m_beepLong.setVolume(0.7f);
    // Sonnerie d'alarme 5 s (arrêt batterie à 20 %)
    m_alarm.setSource(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/beep-5s.wav")));
    m_alarm.setVolume(0.8f);

    // Diagnostic : logue le chargement de chaque effet (Null/Loading/Ready/Error)
    for (QSoundEffect *e : { &m_beep, &m_beepLong, &m_alarm }) {
        connect(e, &QSoundEffect::statusChanged, this, [e]() {
            qWarning() << "[gpsd] sound status:" << e->source().fileName()
                       << "status=" << static_cast<int>(e->status());
        });
    }

    // GPS
    m_gps = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_gps) {
        qWarning() << "[gpsd] no position source — abort";
        QCoreApplication::quit();
        return;
    }
    qWarning() << "[gpsd] position source OK:" << m_gps->sourceName();
    // Source de position selon gpsd.conf (défaut auto = satellite + réseau)
    applyPositioningMode();
    // 1 fix toutes les 5 s en roulant : à 20 km/h = un point tous les 28 m,
    // très suffisant pour du vélo. À l'arrêt, on passe à 30 s (voir
    // onPositionUpdated). Intervalle plus espacé = moins de conso batterie
    // (le GPS est le 2e poste de conso après l'écran).
    m_gps->setUpdateInterval(kIntervalMovingMs);
    // Dernière position connue en cache (sans attendre le serveur) : position
    // de secours affichée tant que le premier fix satellite n'est pas là.
    const QGeoPositionInfo lastKnown = m_gps->lastKnownPosition(false);
    if (lastKnown.isValid() && lastKnown.coordinate().isValid()) {
        m_lastKnownLat = lastKnown.coordinate().latitude();
        m_lastKnownLon = lastKnown.coordinate().longitude();
        m_hasLastKnown = true;
        qWarning() << "[gpsd] lastKnownPosition:" << m_lastKnownLat << m_lastKnownLon;
    }
    connect(m_gps, &QGeoPositionInfoSource::positionUpdated,
            this, &GpsDaemon::onPositionUpdated);

    // Timers espacés pour limiter les réveils CPU / écritures disque
    m_autoSaveTimer.setInterval(300000);  // 300 s — GPX complet (filet secondaire)
    m_partTimer.setInterval(10000);       // 10 s — append incrémental (filet crash)
    m_statsTimer.setInterval(5000);       // 5 s
    m_beepTimer.setInterval(30000);       // 30 s
    m_controlTimer.setInterval(5000);     // 5 s
    m_safetyTimer.setInterval(60000);     // 60 s — batterie + durée max

    connect(&m_autoSaveTimer, &QTimer::timeout, this, &GpsDaemon::onAutoSave);
    connect(&m_partTimer, &QTimer::timeout, this, &GpsDaemon::onPartTick);
    connect(&m_statsTimer, &QTimer::timeout, this, &GpsDaemon::onStatsTick);
    connect(&m_beepTimer, &QTimer::timeout, this, &GpsDaemon::onBeepTick);
    connect(&m_controlTimer, &QTimer::timeout, this, &GpsDaemon::onControlTick);
    connect(&m_safetyTimer, &QTimer::timeout, this, &GpsDaemon::onSafetyTick);
}

GpsDaemon::~GpsDaemon()
{
    cleanup();
}

void GpsDaemon::run()
{
    g_daemon = this;
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
    // Crash : on tente de sauvegarder avant de mourir (best effort — si le
    // handler lui-même plante, la garde g_inSignal fait _exit proprement).
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGBUS, signalHandler);
    std::signal(SIGFPE, signalHandler);

    // Sécurité : le noyau envoie SIGTERM à ce processus quand le parent
    // (l'app QML) meurt — même en cas de crash ou kill -9 de l'app.
    // PR_SET_PDEATHSIG ne fonctionne que si le parent est encore vivant
    // au moment de l'appel, d'où la double vérification.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1) {
        // Parent déjà mort (lancé via startDetached) — on vérifie le PID
        // dans le fichier de contrôle à la place.
        qWarning() << "[gpsd] parent already dead, relying on PID check";
    }

    m_gps->startUpdates();
    m_controlTimer.start();
    qWarning() << "[gpsd] daemon started, appDataDir=" << m_appDataDir
               << "ppid=" << getppid();

    // DEBUG: GPS simulé pour tests sans satellite (intérieur).
    // UTITIBIKE_FAKE_GPS=1 → une position synthétique toutes les secondes.
    if (qEnvironmentVariableIsSet("UTITIBIKE_FAKE_GPS")) {
        m_fakeGpsTimer.setInterval(1000);
        connect(&m_fakeGpsTimer, &QTimer::timeout, this, [this]() {
            static int i = 0;
            ++i;
            QGeoPositionInfo info;
            QGeoCoordinate c(43.6000 + 0.00002 * i, 1.4400 + 0.00002 * i,
                             150.0 + i);
            info.setCoordinate(c);
            info.setTimestamp(QDateTime::currentDateTimeUtc());
            info.setAttribute(QGeoPositionInfo::GroundSpeed, 5.5);
            info.setAttribute(QGeoPositionInfo::Direction, 45.0);
            info.setAttribute(QGeoPositionInfo::VerticalAccuracy, 5.0);
            info.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 8.0);
            onPositionUpdated(info);
        });
        m_fakeGpsTimer.start();
        qWarning() << "[gpsd] FAKE GPS mode ON";
    }
}

void GpsDaemon::onPositionUpdated(const QGeoPositionInfo &info)
{
    // Transition de fix pendant l'enregistrement : son distinct « GPS actif »
    // (fix acquis, bip court) / « GPS perdu » (fix perdu, bip long). Le bip
    // long est une alerte : il sonne même écran éteint (comme playLongBeep) ;
    // les deux respectent l'option gpsd.conf (bip désactivé = aucun son).
    if (m_recording) {
        const bool fix = info.isValid() && info.coordinate().isValid();
        if (fix != m_gotFix) {
            m_gotFix = fix;
            if (readBeepConf()) {
                const QString sndDir = QCoreApplication::applicationDirPath()
                                       + QStringLiteral("/sounds/");
                if (fix) {
                    qWarning() << "[gpsd] GPS ACTIF — bip court";
                    playSound(m_beep, sndDir + QStringLiteral("beep.wav"));
                } else {
                    qWarning() << "[gpsd] GPS PERDU — bip long";
                    playSound(m_beepLong, sndDir + QStringLiteral("beep-long.wav"));
                }
            }
        }
    }
    if (!m_recording || !info.isValid())
        return;

    QGeoCoordinate coord = info.coordinate();
    if (!coord.isValid())
        return;

    const double lat = coord.latitude();
    const double lon = coord.longitude();
    const double alt = coord.altitude();
    const bool altOk = !qIsNaN(alt);
    const double hAcc = info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)
                        ? info.attribute(QGeoPositionInfo::HorizontalAccuracy) : 0.0;


    // Pendant la sonnerie d'arrêt batterie (5 s), on n'ajoute plus de points
    if (m_stopping)
        return;

    const QDateTime fixTime = info.timestamp().isValid()
                              ? info.timestamp()
                              : QDateTime::currentDateTimeUtc();

    // Vitesse implicite depuis le dernier point accepté (km/h) : distingue un
    // vrai saut aberrant (téléportation, fix corrompu) d'une perte de signal
    // GPS suivie d'un déplacement normal (tunnel, forêt, bâtiments) :
    // 500 m en 5 s = 360 km/h (impossible, on rejette), 500 m en 5 min =
    // 6 km/h (crédible à vélo, on accepte).
    double impliedKmh = 0.0;
    if (m_hasLast && m_lastTime.isValid()) {
        const qint64 dtMs = m_lastTime.msecsTo(fixTime);
        const double d0 = haversine(m_lastLat, m_lastLon, lat, lon);
        impliedKmh = dtMs > 0 ? (d0 / 1000.0) / (dtMs / 3600000.0) : 999.0;
    }
    const bool plausible = impliedKmh <= 50.0;  // seuil vélo + marge

    // Filtre qualité AVANT de publier la position : un fix à > 50 m de
    // précision n'écrase pas la dernière bonne position affichée — sauf au
    // tout début, ou si le déplacement depuis le dernier point reste crédible
    // (zone de réception dégradée, pas une téléportation).
    if (hAcc > kMaxAccuracyM && m_hasLast && !plausible) {
        ++m_rejected;
        return;
    }

    // Position courante publiée dans le JSON de stats (pour que l'app suive
    // la carte sans ouvrir une 2e source GPS).
    m_curLat = lat;
    m_curLon = lon;
    m_curValid = true;
    m_curApprox = false;
    m_curSpeed = info.hasAttribute(QGeoPositionInfo::GroundSpeed)
                 ? info.attribute(QGeoPositionInfo::GroundSpeed) : 0.0;
    if (m_curSpeed < 0) m_curSpeed = 0.0;
    m_curAccuracy = hAcc;
    // Auto-pause : à l'arrêt depuis plus de 3 min, on cesse d'ajouter des
    // points (tracé propre, batterie économisée). Reprise automatique dès
    // qu'un mouvement est détecté — par la vitesse GPS (GroundSpeed) OU par
    // la vitesse implicite (déplacement réel depuis le dernier point) : le
    // récepteur renvoie souvent vitesse 0/absente alors qu'on roule, la
    // vitesse seule ne doit pas décider (sinon la pause reste bloquée et
    // le tracé s'arrête avant la fin).
    const bool moving = m_curSpeed >= 1.0 || impliedKmh > 4.0;
    if (moving) {
        m_lastMoveMs = elapsedMs();
        m_paused = false;
    } else if (elapsedMs() - m_lastMoveMs > kPauseAfterMs) {
        m_paused = true;
    }
    // Intervalle adaptatif : à l'arrêt (vitesse < 1 m/s), on espace les
    // fixes à 30 s pour ne pas solliciter le récepteur GPS inutilement ;
    // dès que ça roule, retour à 5 s. Tant qu'on est en pause, on garde
    // 5 s : la reprise de mouvement doit être détectée vite, pas 30 s plus tard.
    m_gps->setUpdateInterval((m_curSpeed < 1.0 && !m_paused) ? kIntervalIdleMs
                                                             : kIntervalMovingMs);
    if (m_paused)
        return;

    // Ignore les sauts GPS aberrants : un écart > 200 m entre 2 points n'est
    // rejeté que si la vitesse implicite est impossible à vélo (> 50 km/h).
    // Après une perte de fix (tunnel, forêt), le prochain fix peut être
    // légitimement loin : sans ce garde-fou temporel, tous les points suivants
    // seraient rejetés (comparés à l'ancien dernier point, jamais mis à jour)
    // et l'enregistrement s'arrêterait définitivement à la sortie de la zone.
    if (m_hasLast) {
        const double d = haversine(m_lastLat, m_lastLon, lat, lon);
        if (d > 200.0 && !plausible)
            return;
        m_distance += d;
        // D+ lissé : médiane des 3 dernières altitudes valides, pour ne pas
        // compter le bruit barométrique comme du dénivelé.
        if (altOk) {
            m_altWin[m_altWinN % 3] = alt;
            ++m_altWinN;
            double w[3] = { m_altWin[0], m_altWin[1], m_altWin[2] };
            const int n = qMin(m_altWinN, 3);
            std::sort(w, w + n);
            const double altEff = w[n / 2];
            if (m_hasAltEff && altEff > m_lastAltEff)
                m_ascent += altEff - m_lastAltEff;
            m_lastAltEff = altEff;
            m_hasAltEff = true;
        }
    }

    QVariantMap p;
    p.insert(QStringLiteral("lat"), lat);
    p.insert(QStringLiteral("lon"), lon);
    if (altOk)
        p.insert(QStringLiteral("ele"), alt);
    p.insert(QStringLiteral("time"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_points.append(p);

    m_lastLat = lat;
    m_lastLon = lon;
    m_lastTime = fixTime;
    m_hasLast = true;
}

// Arrêt propre sur garde-fou disque (plein ou trop d'erreurs d'écriture).
void GpsDaemon::stopOnDiskIssue(const char *reason)
{
    qWarning() << "[gpsd] stopping on disk issue:" << reason
               << "free=" << freeSpaceBytes() << "writeErrors=" << m_writeErrors;
    m_stopReason = QString::fromLatin1(reason);
    cleanup();
}

void GpsDaemon::onAutoSave()
{
    if (!m_recording)
        return;
    if (!checkDiskSpace()) {
        stopOnDiskIssue("diskfull");
        return;
    }
    saveGpx(QStringLiteral("en-cours.gpx"));
    if (m_writeErrors >= kMaxWriteErrors)
        stopOnDiskIssue("writeerror");
}

// Append incrémental des nouveaux points dans en-cours.part (JSONL, une
// ligne par point) : si le daemon est tué brutalement (kill -9, OOM), l'app
// peut reconstruire un GPX depuis ce fichier au prochain démarrage.
void GpsDaemon::onPartTick()
{
    if (!m_recording)
        return;
    if (!checkDiskSpace()) {
        stopOnDiskIssue("diskfull");
        return;
    }
    appendPart();
    if (m_writeErrors >= kMaxWriteErrors)
        stopOnDiskIssue("writeerror");
}

void GpsDaemon::appendPart()
{
    if (m_points.size() <= m_partWritten)
        return;
    QFile f(m_partFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        ++m_writeErrors;
        qWarning() << "[gpsd] part open FAIL" << f.errorString();
        return;
    }
    QByteArray buf;
    for (int i = m_partWritten; i < m_points.size(); ++i) {
        const QVariantMap m = m_points.at(i).toMap();
        QJsonObject o;
        o[QStringLiteral("lat")] = m.value(QStringLiteral("lat")).toDouble();
        o[QStringLiteral("lon")] = m.value(QStringLiteral("lon")).toDouble();
        if (m.contains(QStringLiteral("ele")))
            o[QStringLiteral("ele")] = m.value(QStringLiteral("ele")).toDouble();
        o[QStringLiteral("time")] = m.value(QStringLiteral("time")).toString();
        buf += QJsonDocument(o).toJson(QJsonDocument::Compact);
        buf += '\n';
    }
    const qint64 written = f.write(buf);
    f.close();
    if (written != buf.size() || f.error() != QFile::NoError) {
        ++m_writeErrors;
        qWarning() << "[gpsd] part write FAIL" << f.errorString();
        return;
    }
    m_partWritten = m_points.size();
}

// Espace disque restant sur le volume de appDataDir (statvfs). Retourne -1
// si la lecture échoue (traité comme « inconnu = OK » par checkDiskSpace).
qint64 GpsDaemon::freeSpaceBytes() const
{
    const QStorageInfo si(m_appDataDir);
    if (!si.isValid() || !si.isReady())
        return -1;
    return si.bytesAvailable();
}

// Garde-fou : assez de place pour continuer à écrire (.part, GPX, JSON) ?
bool GpsDaemon::checkDiskSpace()
{
    const qint64 free = freeSpaceBytes();
    if (free >= 0 && free < kMinFreeBytes) {
        qWarning() << "[gpsd] DISK FULL: only" << free << "bytes left";
        return false;
    }
    return true;
}

// Recharge un tracé depuis en-cours.part (reprise après crash du daemon) :
// points, distance, D+, position courante et chrono recalé sur le début du
// tracé. Retourne false si aucun point récupérable.
bool GpsDaemon::loadPartFile()
{
    QFile f(m_partFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray content = f.readAll();
    f.close();
    if (content.isEmpty())
        return false;

    // Première ligne = meta "#session <id>" : ne reprendre le .part que si
    // la session correspond à celle du fichier de contrôle courant.
    const QList<QByteArray> rawLines = content.split('\n');
    int first = 0;
    if (!rawLines.isEmpty() && rawLines.first().startsWith("#session ")) {
        const QString partSession = QString::fromLatin1(rawLines.first().mid(9).trimmed());
        if (!m_sessionId.isEmpty() && partSession != m_sessionId) {
            qWarning() << "[gpsd] .part session" << partSession
                       << "!= session courante" << m_sessionId << "— ignoré";
            return false;
        }
        first = 1;
    }

    QVariantList points;
    double distance = 0.0, ascent = 0.0;
    bool hasLast = false;
    double lastLat = 0.0, lastLon = 0.0, lastAlt = 0.0;
    bool hasAlt = false;
    QDateTime start;
    QDateTime lastTime;
    for (int li = first; li < rawLines.size(); ++li) {
        const QByteArray &line = rawLines.at(li);
        if (line.trimmed().isEmpty() || line.startsWith('#'))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty())
            continue;
        const double lat = o.value(QStringLiteral("lat")).toDouble();
        const double lon = o.value(QStringLiteral("lon")).toDouble();
        const bool eleOk = o.contains(QStringLiteral("ele"));
        const double ele = o.value(QStringLiteral("ele")).toDouble();
        const QString time = o.value(QStringLiteral("time")).toString();
        if (start.isNull() && !time.isEmpty())
            start = QDateTime::fromString(time, Qt::ISODateWithMs);
        if (hasLast) {
            const double d = haversine(lastLat, lastLon, lat, lon);
            if (d <= 200.0) {
                distance += d;
                if (eleOk && hasAlt && ele > lastAlt)
                    ascent += ele - lastAlt;
            }
        }
        QVariantMap p;
        p.insert(QStringLiteral("lat"), lat);
        p.insert(QStringLiteral("lon"), lon);
        if (eleOk)
            p.insert(QStringLiteral("ele"), ele);
        p.insert(QStringLiteral("time"), time);
        points.append(p);
        lastLat = lat; lastLon = lon; lastAlt = ele; hasAlt = eleOk; hasLast = true;
        if (!time.isEmpty())
            lastTime = QDateTime::fromString(time, Qt::ISODateWithMs);
    }
    if (points.isEmpty())
        return false;

    m_points = points;
    m_partWritten = points.size();
    m_distance = distance;
    m_ascent = ascent;
    m_hasLast = true;
    m_lastLat = lastLat;
    m_lastLon = lastLon;
    m_lastTime = lastTime;
    // D+ : on repart de la dernière altitude connue du tracé repris
    if (hasAlt) {
        m_lastAltEff = lastAlt;
        m_hasAltEff = true;
    }
    m_lastMoveMs = elapsedMs();
    // Position courante = dernier point du tracé repris
    m_curLat = lastLat;
    m_curLon = lastLon;
    m_curValid = true;
    m_curApprox = false;
    if (start.isValid()) {
        m_startTime = start;
        m_elapsedOffsetMs = start.msecsTo(QDateTime::currentDateTimeUtc());
    }
    return true;
}

// Batterie via UPower (system bus). Retourne -1 si la lecture échoue.
double GpsDaemon::batteryPct() const
{
    QDBusMessage msg2 = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.UPower"),
                QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("Get"));
    msg2.setArguments({ QStringLiteral("org.freedesktop.UPower.Device"),
                        QStringLiteral("Percentage") });
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg2, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
        const QVariant v = reply.arguments().first();  // QDBusVariant(Percentage)
        const double pct = v.value<QDBusVariant>().variant().toDouble();
        if (pct >= 0.0 && pct <= 100.0)
            return pct;
    }
    // Fallback sysfs si le bus est bloqué par AppArmor
    const QDir psDir(QStringLiteral("/sys/class/power_supply"));
    const QStringList entries = psDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries) {
        QFile f(psDir.filePath(e + QStringLiteral("/capacity")));
        if (f.open(QIODevice::ReadOnly)) {
            const double v = QString::fromLatin1(f.readAll().trimmed()).toDouble();
            f.close();
            if (v >= 0.0 && v <= 100.0)
                return v;
        }
    }
    return -1.0;
}

// État de l'écran via logind : la session seat0 est "Active" quand l'écran
// est allumé. Si la lecture échoue, on considère l'écran allumé (le beep
// reste actif par défaut — comportement sûr).
bool GpsDaemon::screenIsOn() const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"),
                QStringLiteral("ListSessions"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
        const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::ArrayType) {
            QDBusObjectPath seat0;
            arg.beginArray();
            while (!arg.atEnd()) {
                arg.beginStructure();
                QString id; uint uid = 0; QString user; QString seat; QDBusObjectPath path;
                arg >> id >> uid >> user >> seat >> path;
                arg.endStructure();
                if (seat == QStringLiteral("seat0"))
                    seat0 = path;
            }
            arg.endArray();
            if (!seat0.path().isEmpty()) {
                QDBusMessage pmsg = QDBusMessage::createMethodCall(
                            QStringLiteral("org.freedesktop.login1"), seat0.path(),
                            QStringLiteral("org.freedesktop.DBus.Properties"),
                            QStringLiteral("Get"));
                pmsg.setArguments({ QStringLiteral("org.freedesktop.login1.Session"),
                                    QStringLiteral("Active") });
                const QDBusMessage preply = QDBusConnection::systemBus().call(pmsg, QDBus::Block, 2000);
                if (preply.type() != QDBusMessage::ErrorMessage && !preply.arguments().isEmpty())
                    return preply.arguments().first().value<QDBusVariant>().variant().toBool();
            }
        }
    }
    // Fallback sysfs si le bus est bloqué par AppArmor : luminosité > 0 = allumé.
    // Un téléphone peut exposer plusieurs backlights (ex. acpi_video0 à 0 +
    // le vrai rétroéclairage) : on prend le MAX de tous, pas le premier,
    // sinon un entrée à 0 fait croire à tort que l'écran est éteint.
    const QDir blDir(QStringLiteral("/sys/class/backlight"));
    const QStringList entries = blDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int maxBrightness = -1;
    for (const QString &e : entries) {
        QFile f(blDir.filePath(e + QStringLiteral("/actual_brightness")));
        if (f.open(QIODevice::ReadOnly)) {
            const int v = QString::fromLatin1(f.readAll().trimmed()).toInt();
            f.close();
            maxBrightness = qMax(maxBrightness, v);
        }
    }
    if (maxBrightness >= 0)
        return maxBrightness > 0;
    return true;  // inconnu → on considère l'écran allumé (beep actif, sûr)
}

bool GpsDaemon::readBeepConf() const
{
    QFile f(m_confFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;  // pas de conf = beep actif par défaut
    const QByteArray content = f.readAll();
    f.close();
    return !content.contains("beep=0");
}

// Applique la source de position choisie dans gpsd.conf (positioning=...) :
// auto = satellite + WiFi/cell, satellite = GPS seul, network = réseau seul.
void GpsDaemon::applyPositioningMode()
{
    QString mode = QStringLiteral("auto");
    QFile f(m_confFile);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray content = f.readAll();
        f.close();
        const QList<QByteArray> lines = content.split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("positioning="))
                mode = QString::fromLatin1(line.mid(12).trimmed());
        }
    }
    if (mode == QLatin1String("satellite"))
        m_gps->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
    else if (mode == QLatin1String("network")) {
        m_gps->setPreferredPositioningMethods(QGeoPositionInfoSource::NonSatellitePositioningMethods);
        // Secours : si le mode réseau seul ne fournit aucun fix après 30 s
        // (le provider réseau de UT peut être indisponible), on retombe sur
        // toutes les méthodes (satellite inclus) — sinon aucune position
        // n'est jamais trouvée en mode « réseau ».
        QTimer::singleShot(30000, this, [this]() {
            if (!m_gotFix) {
                qWarning() << "[gpsd] mode réseau sans fix après 30 s — secours satellite (AllPositioningMethods)";
                m_gps->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
            }
        });
    } else
        m_gps->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
    qWarning() << "[gpsd] positioning mode:" << mode;
}

void GpsDaemon::onSafetyTick()
{
    if (!m_recording)
        return;
    // Durée max 12 h : évite un daemon oublié qui tourne indéfiniment.
    if (elapsedMs() > kMaxDurationMs) {
        qWarning() << "[gpsd] max duration 12h reached — stopping";
        m_stopReason = QStringLiteral("maxduration");
        cleanup();
        return;
    }
    // Batterie <= 20 % : sonnerie d'alerte 5 s (même écran éteint, même si
    // bip désactivé — c'est une alarme d'arrêt), puis sauvegarde et arrêt.
    const double pct = batteryPct();
    if (pct >= 0.0 && pct <= kBatteryLowPct && !m_alarmPlaying) {
        m_alarmPlaying = true;
        m_stopping = true;   // plus de nouveaux points pendant la sonnerie
        m_stopReason = QStringLiteral("battery");
        qWarning() << "[gpsd] battery" << pct << "% <= 20% — ALARM 5s, then save+stop";
        playAlarm();
        QTimer::singleShot(kAlarmDurationMs, this, [this]() { cleanup(); });
        return;
    }
}

// Sonnerie d'alarme 5 s : jouée quoi qu'il arrive (écran éteint, bip coupé) —
// c'est une alarme d'arrêt de l'enregistrement, pas une confirmation.
void GpsDaemon::playAlarm()
{
    playSound(m_alarm, QCoreApplication::applicationDirPath()
                       + QStringLiteral("/sounds/beep-5s.wav"));
}

// Notification système (session bus, async — échoue silencieusement si
// AppArmor bloque). Le daemon tourne toujours, même app gelée : c'est lui
// qui notifie les événements impactants de l'enregistrement.
void GpsDaemon::sendNotification(const QString &body) const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("Notify"));
    msg.setArguments({ QStringLiteral("utitibike"),
                       QVariant(uint(0)),
                       QStringLiteral("emblem-ok"),
                       QStringLiteral("Utitibike"),
                       body,
                       QVariantList(),
                       QVariantMap(),
                       QVariant(int(5000)) });
    QDBusConnection::sessionBus().asyncCall(msg);
}

void GpsDaemon::onStatsTick()
{
    if (!m_recording)
        return;
    writeStatsJson();
    if (m_writeErrors >= kMaxWriteErrors)
        stopOnDiskIssue("writeerror");
}

// Joue un wav : paplay (PulseAudio) en PRIORITÉ — dans le daemon détaché
// (QCoreApplication), QSoundEffect charge le wav (status Ready) mais ne sort
// AUCUN son audible (backend audio Qt Multimedia non initialisé hors
// QGuiApplication) : constaté sur téléphone, aucun bip entendu alors que le
// log disait « played via QSoundEffect ». paplay seul est fiable ici.
// Fallback QSoundEffect si paplay est absent du système.
void GpsDaemon::playSound(QSoundEffect &effect, const QString &wavPath)
{
    static const QString paplay = QStandardPaths::findExecutable(QStringLiteral("paplay"));
    if (!paplay.isEmpty()) {
        QProcess::startDetached(paplay, { wavPath });
        qWarning() << "[gpsd] sound played via paplay:" << wavPath;
        return;
    }
    if (effect.status() == QSoundEffect::Ready || effect.status() == QSoundEffect::Loading) {
        effect.play();
        qWarning() << "[gpsd] sound played via QSoundEffect (fallback):" << wavPath
                   << "status=" << static_cast<int>(effect.status());
        return;
    }
    qWarning() << "[gpsd] no audio path available for:" << wavPath;
}

void GpsDaemon::onBeepTick()
{
    if (!m_recording)
        return;
    // Bip désactivable par l'utilisateur (gpsd.conf), et coupé quand
    // l'écran est éteint (inutile et réveille la stack audio).
    const bool beepOn = readBeepConf();
    const bool screenOn = screenIsOn();
    qWarning() << "[gpsd] beep tick: beepConf=" << beepOn << "screenOn=" << screenOn
               << "beepStatus=" << static_cast<int>(m_beep.status())
               << "muted=" << m_beep.isMuted();
    if (!beepOn)
        return;
    if (!screenOn)
        return;
    playSound(m_beep, QCoreApplication::applicationDirPath()
                       + QStringLiteral("/sounds/beep.wav"));
}

// Beep long d'alerte : joue MEME si l'écran est éteint — c'est une alerte
// « action requise » (départ manuel), l'utilisateur doit réagir.
// Respecte quand même l'option gpsd.conf (bip désactivé = aucun son).
void GpsDaemon::playLongBeep()
{
    if (!readBeepConf())
        return;
    qWarning() << "[gpsd] LONG BEEP — départ manuel proposé";
    const QString longPath = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/sounds/beep-long.wav");
    playSound(m_beepLong, longPath);
    // 2e coup après 1,4 s (QSoundEffect Qt5 n'a pas setLoops)
    QTimer::singleShot(1400, this, [this, longPath]() { playSound(m_beepLong, longPath); });
}

void GpsDaemon::onControlTick()
{
    const bool controlExists = QFile::exists(m_controlFile);

    if (controlExists && !m_recording) {
        // Démarrage demandé par l'app — lit le PID parent depuis le fichier
        QFile cf(m_controlFile);
        if (cf.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> parts = cf.readAll().simplified().split(' ');
            cf.close();
            m_parentPid = parts.value(0).toInt();
            if (parts.size() > 1)
                m_sessionId = QString::fromLatin1(parts.at(1));
        }
        qWarning() << "[gpsd] parent PID from control file:" << m_parentPid
                   << "session" << m_sessionId;

        m_recording = true;
        m_distance = 0.0;
        m_ascent = 0.0;
        m_hasLast = false;
        m_lastTime = QDateTime();
        m_paused = false;
        m_lastMoveMs = elapsedMs();  // init au start : la pause auto fonctionne
                                      // même si on démarre à l'arrêt
        m_gotFix = false;
        m_rejected = 0;
        m_altWinN = 0;
        m_hasAltEff = false;
        m_partWritten = 0;
        m_awaitManualStart = false;
        m_alarmPlaying = false;
        m_stopping = false;
        m_stopReason = QStringLiteral("user");
        m_elapsedOffsetMs = 0;
        m_curValid = false;
        m_curApprox = false;
        m_curSpeed = 0.0;
        m_curAccuracy = 0.0;
        m_gps->setUpdateInterval(kIntervalMovingMs);
        applyPositioningMode();  // relit gpsd.conf au cas où le réglage a changé
        m_startTime = QDateTime::currentDateTimeUtc();
        m_elapsed.start();

        // Reprise après crash : si un en-cours.part existe ET porte la même
        // session (daemon tué puis relancé par l'app avec le même control
        // file), on recharge le tracé et on continue. Sinon (nouvel
        // enregistrement, session différente), on ignore le .part — un vieux
        // tracé ne doit JAMAIS être repris dans une nouvelle session.
        if (QFile::exists(m_partFile) && QFileInfo(m_partFile).size() > 0 && loadPartFile()) {
            qWarning() << "[gpsd] RESUME from en-cours.part:" << m_points.size()
                       << "points, dist=" << m_distance << "m";
            sendNotification(tr("Enregistrement repris après un crash"));
        } else {
            m_points.clear();
            // Position de départ : dernier fix reçu si on en a un, sinon la
            // dernière position connue (approx) en attendant le premier fix.
            if (m_hasLastKnown) {
                m_curLat = m_lastKnownLat;
                m_curLon = m_lastKnownLon;
                m_curValid = true;
                m_curApprox = true;
            }
            QFile::remove(m_partFile);
            QFile::remove(m_tracksDir + QStringLiteral("/en-cours.gpx"));
            // Nouveau .part avec la meta de session en première ligne
            QFile nf(m_partFile);
            if (nf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                nf.write("#session " + m_sessionId.toLatin1() + "\n");
                nf.close();
            }
        }
        m_autoSaveTimer.start();
        m_partTimer.start();
        m_statsTimer.start();
        m_beepTimer.start();
        m_safetyTimer.start();
        writeStatsJson();
        qWarning() << "[gpsd] recording STARTED";
        sendNotification(tr("Enregistrement démarré"));
    } else if (m_recording) {
        // Vérifie si l'arrêt a été demandé (fichier supprimé)
        if (!controlExists) {
            qWarning() << "[gpsd] recording STOPPED by control file removal";
            m_stopReason = QStringLiteral("control");
            cleanup();
            return;
        }
        // Sécurité secours : vérifie que l'app (parent) est encore vivante
        if (m_parentPid > 0 && kill(m_parentPid, 0) != 0) {
            qWarning() << "[gpsd] parent app dead (pid" << m_parentPid
                       << ") — stopping recording";
            m_stopReason = QStringLiteral("parent");
            cleanup();
            return;
        }
        // Départ manuel : l'app a écrit "lat lon" dans manual-start.control
        // (aucun fix satellite depuis le début de l'enregistrement).
        if (!m_hasLast && QFile::exists(m_manualStartFile)) {
            QFile mf(m_manualStartFile);
            if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QList<QByteArray> parts = mf.readAll().simplified().split(' ');
                mf.close();
                if (parts.size() >= 2) {
                    const double mlat = parts.at(0).toDouble();
                    const double mlon = parts.at(1).toDouble();
                    if (QGeoCoordinate(mlat, mlon).isValid()) {
                        QVariantMap p;
                        p.insert(QStringLiteral("lat"), mlat);
                        p.insert(QStringLiteral("lon"), mlon);
                        p.insert(QStringLiteral("time"),
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                        m_points.append(p);
                        m_lastLat = mlat;
                        m_lastLon = mlon;
                        m_hasLast = true;
                        m_curLat = mlat;
                        m_curLon = mlon;
                        m_curValid = true;
                        m_curApprox = true;
                        m_awaitManualStart = false;
                        qWarning() << "[gpsd] manual start set to" << mlat << mlon;
                    }
                }
            }
            QFile::remove(m_manualStartFile);
        }
        // Après 6 min sans aucun fix, on propose à l'app un départ manuel
        // (touche sur la carte) + beep long d'alerte au moment du passage.
        if (!m_gotFix && elapsedMs() > kManualStartAfterMs && !m_awaitManualStart) {
            m_awaitManualStart = true;
            playLongBeep();
            sendNotification(tr("Pas de signal GPS — touchez la carte pour définir le départ"));
        } else if (m_awaitManualStart && (m_gotFix || m_hasLast)) {
            // Un fix est arrivé ou le départ manuel a été posé : on retire la demande.
            m_awaitManualStart = false;
        }
    }
}

void GpsDaemon::writeStatsJson()
{
    const int secs = elapsedMs() / 1000;
    const double speedKmh = secs > 0 ? (m_distance / 1000.0) / (secs / 3600.0) : 0.0;

    QJsonObject obj;
    obj[QStringLiteral("recording")] = true;
    obj[QStringLiteral("distanceM")] = m_distance;
    obj[QStringLiteral("ascentM")] = m_ascent;
    obj[QStringLiteral("elapsedSec")] = secs;
    obj[QStringLiteral("avgSpeedKmh")] = speedKmh;
    obj[QStringLiteral("pointCount")] = m_points.size();
    obj[QStringLiteral("startTime")] = m_startTime.toString(Qt::ISODateWithMs);
    // Position courante pour l'UI (suivi carte + vitesse instantanée sans
    // ouvrir une 2e source GPS côté app).
    obj[QStringLiteral("lat")] = m_curLat;
    obj[QStringLiteral("lon")] = m_curLon;
    obj[QStringLiteral("hasFix")] = m_curValid && !m_curApprox;
    obj[QStringLiteral("approx")] = m_curValid && m_curApprox;
    obj[QStringLiteral("speedKmh")] = m_curSpeed * 3.6;
    obj[QStringLiteral("accuracyM")] = m_curAccuracy;
    obj[QStringLiteral("paused")] = m_paused;
    obj[QStringLiteral("awaitManualStart")] = m_awaitManualStart;
    obj[QStringLiteral("rejected")] = m_rejected;
    // Garde-fous visibles côté app
    obj[QStringLiteral("writeError")] = m_writeErrors > 0;
    obj[QStringLiteral("freeBytes")] = freeSpaceBytes();

    QSaveFile f(m_statsFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        if (f.commit()) {
            m_writeErrors = 0;  // erreurs consécutives : reset sur succès
        } else {
            ++m_writeErrors;
            qWarning() << "[gpsd] stats commit FAIL";
        }
    } else {
        ++m_writeErrors;
        qWarning() << "[gpsd] stats open FAIL" << f.errorString();
    }
}

void GpsDaemon::writeDoneJson(const QString &gpxPath)
{
    QJsonObject obj;
    obj[QStringLiteral("recording")] = false;
    obj[QStringLiteral("distanceM")] = m_distance;
    obj[QStringLiteral("ascentM")] = m_ascent;
    obj[QStringLiteral("elapsedSec")] = elapsedMs() / 1000;
    obj[QStringLiteral("pointCount")] = m_points.size();
    obj[QStringLiteral("startTime")] = m_startTime.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("gpxPath")] = gpxPath;
    obj[QStringLiteral("gpxContent")] = generateGpx();
    obj[QStringLiteral("stopReason")] = m_stopReason;

    QSaveFile f(m_doneFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        if (!f.commit()) {
            ++m_writeErrors;
            qWarning() << "[gpsd] done commit FAIL";
        }
    } else {
        ++m_writeErrors;
        qWarning() << "[gpsd] done open FAIL" << f.errorString();
    }
}

QString GpsDaemon::generateGpx() const
{
    if (m_points.size() < 2)
        return QString();

    QString gpx;
    QXmlStreamWriter w(&gpx);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("utitibike-gpsd"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    w.writeStartElement(QStringLiteral("trk"));
    w.writeTextElement(QStringLiteral("name"),
                       m_startTime.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    w.writeStartElement(QStringLiteral("trkseg"));
    for (const QVariant &v : m_points) {
        const QVariantMap m = v.toMap();
        w.writeStartElement(QStringLiteral("trkpt"));
        w.writeAttribute(QStringLiteral("lat"),
                         QString::number(m.value(QStringLiteral("lat")).toDouble(), 'f', 7));
        w.writeAttribute(QStringLiteral("lon"),
                         QString::number(m.value(QStringLiteral("lon")).toDouble(), 'f', 7));
        if (m.contains(QStringLiteral("ele")))
            w.writeTextElement(QStringLiteral("ele"),
                               QString::number(m.value(QStringLiteral("ele")).toDouble(), 'f', 1));
        w.writeTextElement(QStringLiteral("time"),
                           m.value(QStringLiteral("time")).toString());
        w.writeEndElement(); // trkpt
    }
    w.writeEndElement(); // trkseg
    w.writeEndElement(); // trk
    w.writeEndElement(); // gpx
    w.writeEndDocument();
    return gpx;
}

QString GpsDaemon::saveGpx(const QString &fileName) const
{
    const QString gpx = generateGpx();
    if (gpx.isEmpty())
        return QString();

    const QString path = m_tracksDir + QLatin1Char('/') + fileName;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        ++m_writeErrors;
        qWarning() << "[gpsd] save open FAIL" << path << f.errorString();
        return QString();
    }
    f.write(gpx.toUtf8());
    if (!f.commit()) {
        ++m_writeErrors;
        qWarning() << "[gpsd] save FAIL" << path;
        return QString();
    }
    return path;
}

void GpsDaemon::cleanup()
{
    if (!m_recording) {
        // Arrêt demandé avant démarrage — quitte directement
        QCoreApplication::quit();
        return;
    }

    m_recording = false;
    m_autoSaveTimer.stop();
    m_partTimer.stop();
    m_statsTimer.stop();
    m_beepTimer.stop();
    m_controlTimer.stop();
    m_safetyTimer.stop();
    m_gps->stopUpdates();

    // Sauvegarde finale
    const QString name = QStringLiteral("parcours-")
                         + m_startTime.toString(QStringLiteral("yyyyMMdd-HHmmss"))
                         + QStringLiteral(".gpx");
    const QString gpxPath = saveGpx(name);
    writeDoneJson(gpxPath);

    // Nettoie les fichiers de contrôle et les filets de sécurité
    QFile::remove(m_controlFile);
    QFile::remove(m_statsFile);
    QFile::remove(m_partFile);
    QFile::remove(m_manualStartFile);
    QFile::remove(m_tracksDir + QStringLiteral("/en-cours.gpx"));

    qWarning() << "[gpsd] saved" << gpxPath << m_points.size() << "points,"
               << "stopReason=" << m_stopReason << "exiting";

    // Notification d'arrêt pour les auto-stops (le daemon tourne même si
    // l'app est gelée). Les arrêts manuels sont notifiés par l'app.
    if (m_stopReason == QLatin1String("battery"))
        sendNotification(tr("Batterie à 20 % — enregistrement arrêté, parcours sauvegardé"));
    else if (m_stopReason == QLatin1String("diskfull"))
        sendNotification(tr("Disque plein — enregistrement arrêté, parcours sauvegardé"));
    else if (m_stopReason == QLatin1String("writeerror"))
        sendNotification(tr("Erreur d'écriture disque — enregistrement arrêté, parcours sauvegardé"));
    else if (m_stopReason == QLatin1String("maxduration"))
        sendNotification(tr("Durée max (12 h) atteinte — enregistrement arrêté, parcours sauvegardé"));

    // Arrêt batterie : on ferme aussi l'app (même gelée écran éteint) pour
    // qu'elle importe le parcours dans l'historique à sa fermeture.
    if (m_stopReason == QLatin1String("battery") && m_parentPid > 0) {
        qWarning() << "[gpsd] battery stop — SIGTERM to parent app pid" << m_parentPid;
        kill(m_parentPid, SIGTERM);
    }

    QCoreApplication::quit();
}
