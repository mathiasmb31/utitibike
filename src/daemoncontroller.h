#ifndef DAEMONCONTROLLER_H
#define DAEMONCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QDateTime>

// Contrôle le daemon GPS détaché (utitibike-gpsd) depuis l'app QML.
// Remplace RouteRecorder pour le mode enregistrement : le daemon tourne
// sans UI, l'app peut se fermer (le daemon reçoit SIGTERM via
// PR_SET_PDEATHSIG, ou détecte la mort du parent via le PID dans
// recording.control).
//
// Communication par fichiers dans appDataDir :
//   recording.control  — "<pid>" écrit par l'app, supprimé = arrêt demandé
//   recording.json     — stats live écrites par le daemon, lues ici
//   recording.done     — résumé final après arrêt (GPX path + contenu)
class DaemonController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double distanceM READ distanceM NOTIFY statsChanged)
    Q_PROPERTY(double ascentM READ ascentM NOTIFY statsChanged)
    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY statsChanged)
    Q_PROPERTY(double avgSpeedKmh READ avgSpeedKmh NOTIFY statsChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY statsChanged)
    // Dernier GPX sauvegardé (disponible après stop, pour l'historique)
    Q_PROPERTY(QString lastGpxPath READ lastGpxPath NOTIFY doneReceived)
    Q_PROPERTY(QString lastGpxContent READ lastGpxContent NOTIFY doneReceived)

public:
    explicit DaemonController(QObject *parent = nullptr);

    bool recording() const { return m_recording; }
    double distanceM() const { return m_distance; }
    double ascentM() const { return m_ascent; }
    int elapsedSec() const { return m_elapsedSec; }
    double avgSpeedKmh() const { return m_avgSpeed; }
    int pointCount() const { return m_pointCount; }
    QString lastGpxPath() const { return m_lastGpxPath; }
    QString lastGpxContent() const { return m_lastGpxContent; }

    // Démarre l'enregistrement : crée le fichier de contrôle avec le PID,
    // lance le daemon. Si le daemon est déjà vivant (redémarrage app),
    // se resynchronise juste sur les stats.
    Q_INVOKABLE void startRecording();
    // Arrête l'enregistrement : supprime le fichier de contrôle + SIGTERM.
    Q_INVOKABLE void stopRecording();
    // Vérifie si un enregistrement daemon est en cours (appelé au
    // démarrage de l'app pour se resynchroniser).
    Q_INVOKABLE void checkExistingRecording();

signals:
    void recordingChanged();
    void statsChanged();
    void doneReceived();  // recording.done reçu (après stop)

private slots:
    void onPollTimer();
    void onDaemonFinished(int exitCode, QProcess::ExitStatus status);
    void onDaemonError(QProcess::ProcessError error);

private:
    void readStatsJson();
    void readDoneJson();
    void updateFromJson(const QJsonObject &obj);
    void cleanupFiles();

    QTimer m_pollTimer;       // 2 s — lit recording.json
    QProcess *m_daemon = nullptr;  // processus fils (PR_SET_PDEATHSIG)
    bool m_recording = false;
    double m_distance = 0.0;
    double m_ascent = 0.0;
    int m_elapsedSec = 0;
    double m_avgSpeed = 0.0;
    int m_pointCount = 0;
    QString m_lastGpxPath;
    QString m_lastGpxContent;
    QString m_appDataDir;
    QString m_controlFile;
    QString m_statsFile;
    QString m_doneFile;
};

#endif // DAEMONCONTROLLER_H
