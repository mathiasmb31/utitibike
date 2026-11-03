#include "daemoncontroller.h"
#include "geo.h"
#include "routehistory.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamWriter>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingCall>
#include <QDebug>
#include <unistd.h>

DaemonController::DaemonController(QObject *parent)
    : QObject(parent)
{
    m_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qWarning() << "[daemonctl] ctor appDataDir=" << m_appDataDir
               << "appName=" << QCoreApplication::applicationName();
    m_controlFile = m_appDataDir + QStringLiteral("/recording.control");
    m_statsFile   = m_appDataDir + QStringLiteral("/recording.json");
    m_doneFile    = m_appDataDir + QStringLiteral("/recording.done");
    m_confFile    = m_appDataDir + QStringLiteral("/gpsd.conf");
    m_manualStartFile = m_appDataDir + QStringLiteral("/manual-start.control");
    m_tracksDir   = m_appDataDir + QStringLiteral("/tracks");

    // Options persistées dans gpsd.conf (lues aussi par le daemon)
    QFile cf(m_confFile);
    if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray content = cf.readAll();
        cf.close();
        m_beepEnabled = !content.contains("beep=0");
        const QList<QByteArray> lines = content.split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("positioning="))
                m_positioningMode = QString::fromLatin1(line.mid(12).trimmed());
        }
    }
    if (m_positioningMode != QLatin1String("satellite")
            && m_positioningMode != QLatin1String("network"))
        m_positioningMode = QStringLiteral("auto");

    // Poll toutes les 5 s pour lire recording.json (moins de réveils CPU
    // côté app pendant l'enregistrement)
    m_pollTimer.setInterval(5000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DaemonController::onPollTimer);

    // Relance du daemon après un crash (single shot, 2 s entre tentatives)
    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(2000);
    connect(&m_restartTimer, &QTimer::timeout, this, &DaemonController::onRestartTimer);
}

void DaemonController::startRecording()
{
    if (m_recording)
        return;

    // Nettoie les anciens fichiers résiduels
    cleanupFiles();
    m_stopRequested = false;
    m_restartAttempts = 0;

    // Écrit le fichier de contrôle : "<pid> <sessionId>". Le daemon vérifie
    // que le PID est vivant (secours de PR_SET_PDEATHSIG) ; le sessionId
    // (timestamp du démarrage) permet au daemon de distinguer une REPRISE
    // après crash (même sessionId → il recharge en-cours.part) d'un NOUVEL
    // enregistrement (sessionId différent → il ignore et supprime le .part,
    // sinon un vieux tracé de test serait repris à tort).
    const QString sessionId = QString::number(QDateTime::currentMSecsSinceEpoch());
    QFile cf(m_controlFile);
    if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[daemonctl] cannot create control file" << m_controlFile
                   << cf.errorString();
        return;
    }
    cf.write(QByteArray::number(QCoreApplication::applicationPid()) + " " + sessionId.toLatin1());
    cf.close();
    qWarning() << "[daemonctl] control file written:" << m_controlFile << "session" << sessionId;

    // Lance le daemon comme fils (pas startDetached) pour que
    // PR_SET_PDEATHSIG fonctionne : le noyau tuera le daemon si l'app meurt.
    if (!launchDaemon()) {
        cleanupFiles();
        return;
    }

    m_recording = true;
    m_distance = 0.0;
    m_ascent = 0.0;
    m_elapsedSec = 0;
    m_avgSpeed = 0.0;
    m_pointCount = 0;
    m_latitude = 0.0;
    m_longitude = 0.0;
    m_hasFix = false;
    m_approx = false;
    m_speedKmh = 0.0;
    m_pollTimer.start();
    emit recordingChanged();
    emit statsChanged();
}

// (Re)lance le processus daemon. Retourne true s'il a démarré.
bool DaemonController::launchDaemon()
{
    if (m_daemon) {
        m_daemon->disconnect(this);
        m_daemon->deleteLater();
        m_daemon = nullptr;
    }
    m_daemon = new QProcess(this);
    connect(m_daemon, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DaemonController::onDaemonFinished);
    connect(m_daemon, &QProcess::errorOccurred,
            this, &DaemonController::onDaemonError);

    const QString daemonPath = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/utitibike-gpsd");
    qWarning() << "[daemonctl] starting daemon:" << daemonPath
               << "exists=" << QFile::exists(daemonPath)
               << "appDirPath=" << QCoreApplication::applicationDirPath();
    m_daemon->setProcessChannelMode(QProcess::ForwardedChannels);
    m_daemon->start(daemonPath, QStringList());

    if (!m_daemon->waitForStarted(3000)) {
        qWarning() << "[daemonctl] daemon failed to start:"
                   << m_daemon->errorString();
        return false;
    }
    qWarning() << "[daemonctl] daemon started, pid=" << m_daemon->processId();
    return true;
}

void DaemonController::stopRecording()
{
    if (!m_recording)
        return;

    // Arrêt demandé par l'utilisateur : empêche toute relance auto
    m_stopRequested = true;
    m_restartTimer.stop();

    // Supprime le fichier de contrôle = signal d'arrêt pour le daemon
    QFile::remove(m_controlFile);

    // Envoie aussi SIGTERM directement pour un arrêt immédiat
    if (m_daemon && m_daemon->state() == QProcess::Running) {
        m_daemon->terminate();  // SIGTERM
        if (!m_daemon->waitForFinished(3000)) {
            m_daemon->kill();   // SIGKILL en dernier recours
            m_daemon->waitForFinished(1000);
        }
    }

    m_recording = false;
    m_pollTimer.stop();

    // Lit le résultat final (le daemon a écrit recording.done avant de quitter)
    readDoneJson();

    emit recordingChanged();
    emit statsChanged();
}

void DaemonController::checkExistingRecording()
{
    // Appelé au démarrage de l'app : si un daemon tourne encore
    // (app redémarrée rapidement), se resynchronise.
    if (!QFile::exists(m_controlFile)) {
        // Pas de contrôle = pas d'enregistrement en cours
        // Nettoie les fichiers résiduels éventuels
        QFile::remove(m_statsFile);
        QFile::remove(m_doneFile);
        return;
    }

    // Vérifie si le daemon tourne encore (le PID dans le fichier est
    // celui de l'ancienne instance de l'app — probablement mort).
    QFile cf(m_controlFile);
    if (cf.open(QIODevice::ReadOnly)) {
        const int oldPid = cf.readAll().trimmed().toInt();
        cf.close();
        // Met à jour avec notre PID pour les futurs contrôles
        if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cf.write(QByteArray::number(QCoreApplication::applicationPid()));
            cf.close();
        }
        qWarning() << "[daemonctl] found existing recording, old app pid:"
                   << oldPid << "new pid:" << QCoreApplication::applicationPid();
    }

    // Essaie de lire les stats existantes pour voir si le daemon tourne
    readStatsJson();
    if (m_pointCount > 0 || m_elapsedSec > 0) {
        // Le daemon tourne encore — se resynchronise
        m_recording = true;
        m_pollTimer.start();
        emit recordingChanged();
        emit statsChanged();
        qWarning() << "[daemonctl] resynchronized with running daemon:"
                   << m_pointCount << "points," << m_elapsedSec << "s";
    } else {
        // Fichier de contrôle existe mais pas de stats — daemon mort
        cleanupFiles();
    }
}

void DaemonController::onPollTimer()
{
    // Supervision : si le daemon est mort sans que l'arrêt ait été demandé
    // (ex. crash pendant que l'app était gelée par Lomiri), on le relance.
    // Le daemon reprendra le tracé depuis en-cours.part.
    if (m_recording && !m_stopRequested && QFile::exists(m_controlFile)
            && m_daemon && m_daemon->state() != QProcess::Running
            && m_restartAttempts < 5 && !m_restartTimer.isActive()) {
        ++m_restartAttempts;
        qWarning() << "[daemonctl] daemon mort détecté au poll — relance, tentative"
                   << m_restartAttempts;
        emit daemonRestarted();
        m_restartTimer.start();
        return;
    }

    readStatsJson();

    // Vérifie aussi si recording.done est arrivé (daemon vient de s'arrêter)
    if (!m_recording && QFile::exists(m_doneFile)) {
        readDoneJson();
    }
}

void DaemonController::onRestartTimer()
{
    if (!m_recording || m_stopRequested || !QFile::exists(m_controlFile))
        return;
    if (launchDaemon()) {
        qWarning() << "[daemonctl] daemon relancé, pid=" << m_daemon->processId();
    } else if (m_restartAttempts < 5) {
        ++m_restartAttempts;
        qWarning() << "[daemonctl] relance échouée, nouvelle tentative"
                   << m_restartAttempts;
        m_restartTimer.start();
    } else {
        qWarning() << "[daemonctl] abandon des relances après 5 tentatives";
    }
}

void DaemonController::onDaemonFinished(int exitCode, QProcess::ExitStatus status)
{
    qWarning() << "[daemonctl] daemon finished, exitCode=" << exitCode
               << "status=" << status;

    if (!m_recording)
        return;

    // Crash du daemon (arrêt non demandé) : relance avec reprise depuis
    // en-cours.part. Cap à 5 tentatives pour éviter une boucle infinie.
    if (!m_stopRequested && QFile::exists(m_controlFile)
            && m_restartAttempts < 5 && !m_restartTimer.isActive()) {
        ++m_restartAttempts;
        qWarning() << "[daemonctl] daemon mort inattendue — relance, tentative"
                   << m_restartAttempts;
        emit daemonRestarted();
        m_restartTimer.start();
        return;
    }

    m_recording = false;
    m_pollTimer.stop();
    readDoneJson();
    emit recordingChanged();
    emit statsChanged();
}

void DaemonController::onDaemonError(QProcess::ProcessError error)
{
    qWarning() << "[daemonctl] daemon error:" << error << m_daemon->errorString();
    if (m_recording) {
        m_recording = false;
        m_pollTimer.stop();
        emit recordingChanged();
    }
}

void DaemonController::readStatsJson()
{
    QFile f(m_statsFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const bool wasRecording = m_recording;
    m_recording = obj.value(QStringLiteral("recording")).toBool();
    m_distance = obj.value(QStringLiteral("distanceM")).toDouble();
    m_ascent = obj.value(QStringLiteral("ascentM")).toDouble();
    m_elapsedSec = obj.value(QStringLiteral("elapsedSec")).toInt();
    m_avgSpeed = obj.value(QStringLiteral("avgSpeedKmh")).toDouble();
    m_pointCount = obj.value(QStringLiteral("pointCount")).toInt();
    m_latitude = obj.value(QStringLiteral("lat")).toDouble();
    m_longitude = obj.value(QStringLiteral("lon")).toDouble();
    m_hasFix = obj.value(QStringLiteral("hasFix")).toBool();
    m_approx = obj.value(QStringLiteral("approx")).toBool();
    m_speedKmh = obj.value(QStringLiteral("speedKmh")).toDouble();
    m_paused = obj.value(QStringLiteral("paused")).toBool();
    m_writeError = obj.value(QStringLiteral("writeError")).toBool();
    m_awaitManualStart = obj.value(QStringLiteral("awaitManualStart")).toBool();

    emit statsChanged();
    if (m_recording != wasRecording)
        emit recordingChanged();
}

void DaemonController::readDoneJson()
{
    QFile f(m_doneFile);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[daemonctl] readDoneJson: no done file" << m_doneFile;
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    m_distance = obj.value(QStringLiteral("distanceM")).toDouble();
    m_ascent = obj.value(QStringLiteral("ascentM")).toDouble();
    m_elapsedSec = obj.value(QStringLiteral("elapsedSec")).toInt();
    m_pointCount = obj.value(QStringLiteral("pointCount")).toInt();
    m_lastGpxPath = obj.value(QStringLiteral("gpxPath")).toString();
    m_lastGpxContent = obj.value(QStringLiteral("gpxContent")).toString();
    m_startTime = obj.value(QStringLiteral("startTime")).toString();
    m_stopReason = obj.value(QStringLiteral("stopReason")).toString("user");
    if (m_stopReason.isEmpty())
        m_stopReason = QStringLiteral("user");

    qWarning() << "[daemonctl] done received:" << m_pointCount << "points,"
               << m_distance << "m," << m_lastGpxPath
               << "gpxContent len=" << m_lastGpxContent.size();

    // Fichier consommé : évite un second doneReceived (stopRecording et
    // onDaemonFinished lisent tous deux le done) → doublon dans l'historique.
    QFile::remove(m_doneFile);

    // Notification système (session bus) : « parcours sauvegardé » pour un
    // arrêt normal. Les auto-stops (battery/diskfull/writeerror/maxduration)
    // sont notifiés par le daemon lui-même (l'app peut être gelée).
    if (!m_lastGpxContent.isEmpty()
            && m_stopReason != QLatin1String("battery")
            && m_stopReason != QLatin1String("diskfull")
            && m_stopReason != QLatin1String("writeerror")
            && m_stopReason != QLatin1String("maxduration")) {
        sendNotification(tr("Parcours sauvegardé : %1 km · D+ %2 m")
                         .arg(m_distance / 1000.0, 0, 'f', 1)
                         .arg(qRound(m_ascent)));
    }

    emit statsChanged();
    emit doneReceived();
}

void DaemonController::importDoneToHistory(RouteHistory *history)
{
    if (!QFile::exists(m_doneFile) || history == nullptr)
        return;
    readDoneJson();
    if (!m_lastGpxContent.isEmpty()) {
        const QString name = tr("Trajet %1 · %2 km")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("dd/MM HH:mm")))
                .arg(m_distance / 1000.0, 0, 'f', 1);
        history->addEntry(name, m_distance, m_ascent, QVariantList(), m_lastGpxContent, m_startTime);
        qWarning() << "[daemonctl] done imported to history at quit:" << m_pointCount << "points";
    }
}

// Notification système via org.freedesktop.Notifications (bus de session).
// Appel async : aucun blocage si le service est absent.
void DaemonController::sendNotification(const QString &body) const
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

void DaemonController::cleanupFiles()
{
    QFile::remove(m_controlFile);
    QFile::remove(m_statsFile);
    QFile::remove(m_doneFile);
}

// Réécrit gpsd.conf en conservant les deux clés (beep + positioning)
void DaemonController::writeConf()
{
    QSaveFile f(m_confFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray("beep=") + (m_beepEnabled ? "1" : "0") + "\n");
        f.write(QByteArray("positioning=") + m_positioningMode.toLatin1() + "\n");
        f.commit();
    }
}

void DaemonController::setBeepEnabled(bool v)
{
    if (m_beepEnabled == v)
        return;
    m_beepEnabled = v;
    writeConf();
    emit beepEnabledChanged();
}

void DaemonController::setPositioningMode(const QString &v)
{
    const QString mode = (v == QLatin1String("satellite") || v == QLatin1String("network"))
                         ? v : QStringLiteral("auto");
    if (m_positioningMode == mode)
        return;
    m_positioningMode = mode;
    writeConf();  // le daemon relit gpsd.conf au prochain démarrage d'enregistrement
    emit positioningModeChanged();
}

void DaemonController::setManualStart(double lat, double lon)
{
    QFile f(m_manualStartFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::number(lat, 'f', 7) + " " + QByteArray::number(lon, 'f', 7));
        f.close();
        qWarning() << "[daemonctl] manual start written:" << lat << lon;
    }
}

// Reconstruit un GPX depuis un en-cours.part laissé par un daemon tué
// brutalement (kill -9, OOM) : le parcours n'est pas perdu.
bool DaemonController::recoverOrphanPart()
{
    const QString partPath = m_tracksDir + QStringLiteral("/en-cours.part");
    QFile f(partPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray content = f.readAll();
    f.close();
    if (content.isEmpty()) {
        QFile::remove(partPath);
        return false;
    }

    QVariantList points;
    double distance = 0.0, ascent = 0.0;
    bool hasLast = false;
    double lastLat = 0.0, lastLon = 0.0, lastAlt = 0.0;
    bool hasAlt = false;
    QDateTime start;
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty())
            continue;
        const double lat = o.value(QStringLiteral("lat")).toDouble();
        const double lon = o.value(QStringLiteral("lon")).toDouble();
        const double ele = o.value(QStringLiteral("ele")).toDouble(0.0);
        const bool eleOk = o.contains(QStringLiteral("ele"));
        const QString time = o.value(QStringLiteral("time")).toString();
        if (start.isNull() && !time.isEmpty())
            start = QDateTime::fromString(time, Qt::ISODateWithMs);
        QVariantMap p;
        p.insert(QStringLiteral("lat"), lat);
        p.insert(QStringLiteral("lon"), lon);
        if (eleOk)
            p.insert(QStringLiteral("ele"), ele);
        p.insert(QStringLiteral("time"), time);
        if (hasLast) {
            const double d = haversine(lastLat, lastLon, lat, lon);
            if (d <= 200.0) {
                distance += d;
                if (eleOk && hasAlt && ele > lastAlt)
                    ascent += ele - lastAlt;
            }
        }
        lastLat = lat; lastLon = lon; lastAlt = ele; hasAlt = eleOk; hasLast = true;
        points.append(p);
    }
    if (points.size() < 2) {
        QFile::remove(partPath);
        return false;
    }
    if (!start.isValid())
        start = QDateTime::currentDateTimeUtc();
    m_startTime = start.toString(Qt::ISODateWithMs);  // date de début du tracé récupéré

    // Génère le GPX (même format que le daemon)
    QString gpx;
    QXmlStreamWriter w(&gpx);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("utitibike-gpsd-recovery"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    w.writeStartElement(QStringLiteral("trk"));
    w.writeTextElement(QStringLiteral("name"),
                       start.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    w.writeStartElement(QStringLiteral("trkseg"));
    for (const QVariant &v : points) {
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
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();

    const QString name = QStringLiteral("parcours-recupere-")
                         + start.toString(QStringLiteral("yyyyMMdd-HHmmss"))
                         + QStringLiteral(".gpx");
    const QString path = m_tracksDir + QLatin1Char('/') + name;
    QSaveFile sf(path);
    bool ok = false;
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(gpx.toUtf8());
        ok = sf.commit();
    }
    QFile::remove(partPath);  // consommé
    if (!ok) {
        qWarning() << "[daemonctl] recovery save FAIL" << path;
        return false;
    }

    m_lastGpxPath = path;
    m_lastGpxContent = gpx;
    m_distance = distance;
    m_ascent = ascent;
    m_pointCount = points.size();
    qWarning() << "[daemonctl] recovered orphan part:" << path << points.size()
               << "points," << distance << "m";
    emit statsChanged();
    return true;
}
