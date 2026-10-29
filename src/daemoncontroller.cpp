#include "daemoncontroller.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <unistd.h>

DaemonController::DaemonController(QObject *parent)
    : QObject(parent)
{
    m_appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_controlFile = m_appDataDir + QStringLiteral("/recording.control");
    m_statsFile   = m_appDataDir + QStringLiteral("/recording.json");
    m_doneFile    = m_appDataDir + QStringLiteral("/recording.done");

    // Poll toutes les 2 s pour lire recording.json
    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DaemonController::onPollTimer);
}

void DaemonController::startRecording()
{
    if (m_recording)
        return;

    // Nettoie les anciens fichiers résiduels
    cleanupFiles();

    // Écrit le fichier de contrôle avec notre PID (le daemon vérifie
    // que ce PID est vivant, en secours de PR_SET_PDEATHSIG).
    QFile cf(m_controlFile);
    if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[daemonctl] cannot create control file" << m_controlFile;
        return;
    }
    cf.write(QByteArray::number(QCoreApplication::applicationPid()));
    cf.close();

    // Lance le daemon comme fils (pas startDetached) pour que
    // PR_SET_PDEATHSIG fonctionne : le noyau tuera le daemon si l'app meurt.
    m_daemon = new QProcess(this);
    connect(m_daemon, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DaemonController::onDaemonFinished);
    connect(m_daemon, &QProcess::errorOccurred,
            this, &DaemonController::onDaemonError);

    const QString daemonPath = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/utitibike-gpsd");
    qWarning() << "[daemonctl] starting daemon:" << daemonPath;
    m_daemon->start(daemonPath, QStringList());

    if (!m_daemon->waitForStarted(3000)) {
        qWarning() << "[daemonctl] daemon failed to start:"
                   << m_daemon->errorString();
        cleanupFiles();
        return;
    }

    m_recording = true;
    m_distance = 0.0;
    m_ascent = 0.0;
    m_elapsedSec = 0;
    m_avgSpeed = 0.0;
    m_pointCount = 0;
    m_pollTimer.start();
    emit recordingChanged();
    emit statsChanged();
}

void DaemonController::stopRecording()
{
    if (!m_recording)
        return;

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
    readStatsJson();

    // Vérifie aussi si recording.done est arrivé (daemon vient de s'arrêter)
    if (!m_recording && QFile::exists(m_doneFile)) {
        readDoneJson();
    }
}

void DaemonController::onDaemonFinished(int exitCode, QProcess::ExitStatus status)
{
    qWarning() << "[daemonctl] daemon finished, exitCode=" << exitCode
               << "status=" << status;

    if (m_recording) {
        m_recording = false;
        m_pollTimer.stop();
        readDoneJson();
        emit recordingChanged();
        emit statsChanged();
    }
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

    emit statsChanged();
    if (m_recording != wasRecording)
        emit recordingChanged();
}

void DaemonController::readDoneJson()
{
    QFile f(m_doneFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
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

    qWarning() << "[daemonctl] done received:" << m_pointCount << "points,"
               << m_distance << "m," << m_lastGpxPath;

    emit statsChanged();
    emit doneReceived();
}

void DaemonController::cleanupFiles()
{
    QFile::remove(m_controlFile);
    QFile::remove(m_statsFile);
    QFile::remove(m_doneFile);
}
