#ifndef DAEMONCONTROLLER_H
#define DAEMONCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QDateTime>

class RouteHistory;

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
    // Position courante publiée par le daemon (suivi carte + vitesse sans
    // ouvrir une 2e source GPS côté app). approx=true = dernière position
    // connue, pas un vrai fix satellite.
    Q_PROPERTY(double latitude READ latitude NOTIFY statsChanged)
    Q_PROPERTY(double longitude READ longitude NOTIFY statsChanged)
    Q_PROPERTY(bool hasFix READ hasFix NOTIFY statsChanged)
    Q_PROPERTY(bool approx READ approx NOTIFY statsChanged)
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY statsChanged)
    // Auto-pause du daemon (arrêt > 3 min détecté)
    Q_PROPERTY(bool paused READ paused NOTIFY statsChanged)
    // Garde-fou disque : le daemon signale des erreurs d'écriture
    Q_PROPERTY(bool writeError READ writeError NOTIFY statsChanged)
    // Le daemon demande un départ manuel (6 min sans fix satellite)
    Q_PROPERTY(bool awaitManualStart READ awaitManualStart NOTIFY statsChanged)
    // Bip de confirmation activé (persisté dans gpsd.conf, lu par le daemon)
    Q_PROPERTY(bool beepEnabled READ beepEnabled WRITE setBeepEnabled NOTIFY beepEnabledChanged)
    // Source GPS : "auto" | "satellite" | "network" (persisté dans gpsd.conf)
    Q_PROPERTY(QString positioningMode READ positioningMode WRITE setPositioningMode NOTIFY positioningModeChanged)
    // Raison du dernier arrêt : "user", "control", "parent", "battery", "maxduration"
    Q_PROPERTY(QString stopReason READ stopReason NOTIFY doneReceived)
    // Date de début de l'enregistrement (ISO UTC, lue dans recording.done)
    Q_PROPERTY(QString startTime READ startTime NOTIFY doneReceived)
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
    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }
    bool hasFix() const { return m_hasFix; }
    bool approx() const { return m_approx; }
    double speedKmh() const { return m_speedKmh; }
    bool paused() const { return m_paused; }
    bool writeError() const { return m_writeError; }
    bool awaitManualStart() const { return m_awaitManualStart; }
    bool beepEnabled() const { return m_beepEnabled; }
    void setBeepEnabled(bool v);
    QString positioningMode() const { return m_positioningMode; }
    void setPositioningMode(const QString &v);
    QString stopReason() const { return m_stopReason; }
    QString startTime() const { return m_startTime; }
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
    // Départ manuel (pas de fix GPS après 6 min) : écrit "lat lon" dans
    // manual-start.control, lu par le daemon au prochain tick de contrôle.
    Q_INVOKABLE void setManualStart(double lat, double lon);
    // Récupère un en-cours.part laissé par un daemon tué brutalement :
    // reconstruit un GPX, le sauve, remplit lastGpx*/distance/ascent.
    // Retourne true si un parcours a été récupéré.
    Q_INVOKABLE bool recoverOrphanPart();
    // Appelé à la fermeture de l'app (aboutToQuit) : si un recording.done
    // traîne (ex. arrêt batterie qui a SIGTERM l'app), l'importe dans
    // l'historique directement en C++ (le QML ne tourne plus).
    void importDoneToHistory(RouteHistory *history);
    // Notification système depuis le QML (ex. parcours récupéré après crash)
    Q_INVOKABLE void notify(const QString &body) const { sendNotification(body); }

signals:
    void recordingChanged();
    void statsChanged();
    void doneReceived();  // recording.done reçu (après stop)
    void beepEnabledChanged();
    void positioningModeChanged();
    void daemonRestarted();  // le daemon a crashé et a été relancé (reprise depuis en-cours.part)

private slots:
    void onPollTimer();
    void onDaemonFinished(int exitCode, QProcess::ExitStatus status);
    void onDaemonError(QProcess::ProcessError error);
    void onRestartTimer();

private:
    void readStatsJson();
    void readDoneJson();
    void updateFromJson(const QJsonObject &obj);
    void cleanupFiles();
    void writeConf();  // réécrit gpsd.conf (beep + positioning)
    bool launchDaemon();  // (re)lance le processus utitibike-gpsd
    // Notification système (org.freedesktop.Notifications, session bus)
    void sendNotification(const QString &body) const;

    QTimer m_pollTimer;       // 5 s — lit recording.json
    QTimer m_restartTimer;    // 2 s (single shot) — relance après crash
    QProcess *m_daemon = nullptr;  // processus fils (PR_SET_PDEATHSIG)
    int m_restartAttempts = 0;     // cap à 5 pour éviter une boucle infinie
    bool m_stopRequested = false;  // vrai quand l'utilisateur demande l'arrêt
    bool m_recording = false;
    double m_distance = 0.0;
    double m_ascent = 0.0;
    int m_elapsedSec = 0;
    double m_avgSpeed = 0.0;
    int m_pointCount = 0;
    double m_latitude = 0.0;
    double m_longitude = 0.0;
    bool m_hasFix = false;
    bool m_approx = false;
    double m_speedKmh = 0.0;
    bool m_paused = false;
    bool m_writeError = false;
    bool m_awaitManualStart = false;
    bool m_beepEnabled = true;
    QString m_positioningMode = QStringLiteral("auto");
    QString m_stopReason = QStringLiteral("user");
    QString m_startTime;
    QString m_lastGpxPath;
    QString m_lastGpxContent;
    QString m_appDataDir;
    QString m_controlFile;
    QString m_statsFile;
    QString m_doneFile;
    QString m_confFile;
    QString m_manualStartFile;
    QString m_tracksDir;
};

#endif // DAEMONCONTROLLER_H
