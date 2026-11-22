#ifndef GPSDAEMON_H
#define GPSDAEMON_H

#include <QObject>
#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QVariantList>
#include <QSoundEffect>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusObjectPath>

// Daemon GPS pour utitibike. S'exécute sans UI (QCoreApplication),
// enregistre les positions GPS dans un fichier GPX, écrit un JSON de stats
// live, et émet un beep toutes les 30 s. Lancé par l'app QML via QProcess.
// v0.99.1: SURVIT a la fermeture de l'app (plus de PR_SET_PDEATHSIG ni de
// test "parent mort") — l'enregistrement continue tant que recording.control
// existe. L'app se reconnecte au prochain demarrage via checkExistingRecording().
//
// Fichiers dans appDataDir (~/.local/share/mathias.utitibike/) :
//   recording.control  — créé par l'app pour démarrer, supprimé = arrêt
//   recording.json     — stats live (distance, D+, durée, vitesse, position
//                        courante, hasFix/approx, nb pts)
//   recording.done     — résumé final après arrêt (nom, distance, D+, GPX)
//   tracks/en-cours.gpx — auto-sauvegarde atomique toutes les 120 s
//
// Économie batterie : un seul fix toutes les 5 s en roulant, espacé à 30 s
// à l'arrêt (vitesse < 1 m/s) ; beep toutes les 30 s ; stats toutes les 5 s.
class GpsDaemon : public QObject
{
    Q_OBJECT
public:
    explicit GpsDaemon(QObject *parent = nullptr);
    ~GpsDaemon();

    void run();

    // Appelé depuis le handler de signal (crash) pour marquer la raison d'arrêt
    // v1.0: un SIGTERM pendant l'alarme batterie ne doit pas écraser la
    // raison d'arrêt réelle (le done doit rester "battery").
    void setStopReason(const QString &r) {
        if (m_stopReason == QLatin1String("battery")
                || m_stopReason == QLatin1String("diskfull")
                || m_stopReason == QLatin1String("writeerror")
                || m_stopReason == QLatin1String("maxduration"))
            return;
        m_stopReason = r;
    }

private slots:
    void onPositionUpdated(const QGeoPositionInfo &info);
    void onAutoSave();
    void onPartTick();
    void onStatsTick();
    void onBeepTick();
    void onSafetyTick();
    void onControlTick();
    void cleanup();  // slot : appelé via QMetaObject::invokeMethod depuis le handler SIGTERM

private:
    void writeStatsJson();
    bool writeDoneJson(const QString &gpxPath);  // v0.99.1: renvoie le succes
    QString generateGpx() const;
    QString saveGpx(const QString &fileName) const;
    void appendPart();
    // Recharge en-cours.part (reprise après crash du daemon) : retourne true
    // si des points ont été rechargés.
    bool loadPartFile();
    qint64 elapsedMs() const { return m_elapsed.elapsed() + m_elapsedOffsetMs; }
    bool readBeepConf() const;   // gpsd.conf : beep=0/1 (option utilisateur)
    void applyPositioningMode(); // gpsd.conf : positioning=auto|satellite|network
    // v0.99.1: le service de localisation peut couper la session du plugin Qt
    // ("Connection to location service lost, ending session") — sans reprise,
    // le daemon continue de tourner mais n'ajoute plus AUCUN point.
    void recoverGps();           // relance (puis recrée) la source après coupure
    void playLongBeep();         // alerte départ manuel (joue même écran éteint)
    void playAlarm();            // sonnerie 5 s arrêt batterie (joue toujours)
    // Joue un effet sonore : QSoundEffect si prêt, sinon paplay en fallback
    // (le backend Qt Multimedia du daemon peut ne pas charger le wav).
    void playSound(QSoundEffect &effect, const QString &wavPath);
    // Notification système (org.freedesktop.Notifications, session bus)
    void sendNotification(const QString &body) const;
    bool screenIsOn() const;    // logind : la session seat0 est-elle active ?
    double batteryPct() const;  // UPower : pourcentage batterie (-1 si indispo)
    // Garde-fous disque : espace libre (statvfs) et erreurs d'écriture
    qint64 freeSpaceBytes() const;
    bool checkDiskSpace();      // false si moins de kMinFreeBytes restants
    void stopOnDiskIssue(const char *reason);  // arrêt propre : disque plein / erreurs d'écriture
    static constexpr qint64 kMinFreeBytes = 1024 * 1024;   // 1 Mo mini
    static constexpr int kMaxWriteErrors = 3;              // arrêt propre au-delà

    // Intervalles GPS adaptatifs : 5 s en roulant (à 25 km/h = un point tous
    // ~35 m), 30 s à l'arrêt — le récepteur GPS est le 2e poste de conso.
    static constexpr int kIntervalMovingMs = 5000;
    static constexpr int kIntervalIdleMs = 30000;
    // Auto-pause : arrêt détecté après 3 min sans mouvement (vitesse < 1 m/s)
    static constexpr qint64 kPauseAfterMs = 180000;
    // Départ manuel proposé à l'app après 6 min sans aucun fix satellite
    static constexpr qint64 kManualStartAfterMs = 360000;
    // Watchdog coupure service de localisation : pas de fix pendant 60 s ->
    // stopUpdates/startUpdates ; après 3 tentatives, recréer la source.
    static constexpr qint64 kNoFixRecoveryMs = 60000;
    static constexpr qint64 kRecoveryDebounceMs = 30000;
    static constexpr int kMaxGpsRestarts = 3;
    qint64 m_lastPositionMs = 0;     // horloge murale du dernier fix reçu
    qint64 m_lastRecoveryMs = 0;     // anti-rebond entre deux reprises
    int m_gpsRestarts = 0;           // nb de reprises depuis le début du track
    // v0.99.1: arret apres 1 h sans mouvement OU 1 h sans fix GPS — evite un
    // enregistrement fantome du daemon survivant. Surcharge de test :
    // UTITIBIKE_IDLE_STOP_MS=20000.
    static constexpr qint64 kIdleStopMs = 60LL * 60LL * 1000LL;
    qint64 m_lastRealFixMs = 0;   // dernier VRAI fix (pas une reprise GPS)
    // Arrêt automatique de sécurité : batterie <= 20 % (sonnerie 5 s puis
    // sauvegarde) ou durée > 12 h
    static constexpr double kBatteryLowPct = 20.0;
    static constexpr qint64 kMaxDurationMs = 12LL * 3600LL * 1000LL;
    static constexpr int kAlarmDurationMs = 5200;  // sonnerie 5 s + marge
    // Précision horizontale max acceptée pour un point de tracé (m)
    static constexpr double kMaxAccuracyM = 50.0;

    QGeoPositionInfoSource *m_gps = nullptr;
    QTimer m_autoSaveTimer;    // 300 s — sauvegarde GPX complète périodique
    QTimer m_partTimer;        // 10 s — append des nouveaux points dans en-cours.part
    QTimer m_statsTimer;       // 5 s — écriture du JSON de stats
    QTimer m_beepTimer;        // 30 s — beep de confirmation
    QTimer m_controlTimer;     // 5 s — vérifie recording.control + manual-start
    QTimer m_safetyTimer;      // 60 s — batterie (UPower) + durée max 12 h
    QTimer m_fakeGpsTimer;      // DEBUG — GPS simulé (UTITIBIKE_FAKE_GPS=1)
    QSoundEffect m_beep;
    QSoundEffect m_beepLong;    // alerte « action requise » (départ manuel)
    QSoundEffect m_alarm;       // sonnerie 5 s d'arrêt batterie (20 %)

    bool m_recording = false;
    QVariantList m_points;
    double m_distance = 0.0;
    double m_ascent = 0.0;
    QElapsedTimer m_elapsed;
    QDateTime m_startTime;
    double m_lastLat = 0.0;
    double m_lastLon = 0.0;
    QDateTime m_lastTime;  // timestamp du dernier point accepté (filtre anti-saut)
    bool m_hasLast = false;

    // Position courante publiée dans le JSON (fix réel ou dernière position
    // connue) : permet à l'app d'afficher/suivre la position sans ouvrir une
    // 2e source GPS (double conso batterie).
    double m_curLat = 0.0;
    double m_curLon = 0.0;
    double m_curSpeed = 0.0;      // m/s (dernier fix)
    double m_curAccuracy = 0.0;   // m (précision horizontale du dernier fix)
    bool m_curValid = false;
    bool m_curApprox = false;     // true = dernière position connue, pas un fix
    // Position de secours : cache du provider (lue sans attendre le réseau),
    // utilisée quand aucun fix satellite n'est encore arrivé.
    double m_lastKnownLat = 0.0;
    double m_lastKnownLon = 0.0;
    bool m_hasLastKnown = false;

    // Auto-pause à l'arrêt + filtrage qualité
    bool m_paused = false;
    qint64 m_lastMoveMs = 0;   // elapsed() du dernier fix avec vitesse >= 1 m/s
    bool m_gotFix = false;      // fix VALIDE en ce moment (bips de transition)
    bool m_everHadFix = false;  // v0.99.1: au moins un fix recu ce track — sert
                                // au depart manuel et au secours réseau (non réarmable)
    int m_rejected = 0;        // points rejetés (précision > kMaxAccuracyM)
    double m_altWin[3] = {0.0, 0.0, 0.0};  // fenêtre glissante 3 altitudes (lissage D+)
    int m_altWinN = 0;
    // v0.99.1: reference D+ avec hysteresis 5 m — le bruit d'altitude GPS
    // (mediane sur 3 fixes a 3 Hz = 1 seconde seulement) gonflait le D+
    // et classait « Sportif » un parcours plat.
    double m_ascentRefAlt = 0.0;
    static constexpr double kAltNoiseM = 5.0;
    bool m_hasAltEff = false;

    // Filet de sécurité crash : points ajoutés en append dans en-cours.part
    int m_partWritten = 0;
    // Départ manuel : l'app écrit "lat lon" dans manual-start.control quand
    // aucun fix n'est arrivé après kManualStartAfterMs.
    bool m_awaitManualStart = false;
    // Session d'enregistrement (2e champ du fichier de contrôle) : le .part
    // ne doit être repris après crash que si la session correspond.
    QString m_sessionId;
    QString m_stopReason = QStringLiteral("user");
    // Garde-fou écriture : nb d'échecs consécutifs (QSaveFile/QFile)
    mutable int m_writeErrors = 0;
    // Sonnerie batterie en cours : on n'ajoute plus de points pendant les 5 s
    bool m_alarmPlaying = false;
    bool m_stopping = false;
    // Reprise après crash : offset pour recaler le chrono sur l'heure de début
    // du tracé rechargé depuis en-cours.part.
    qint64 m_elapsedOffsetMs = 0;

    QString m_appDataDir;
    QString m_pidFile;       // v0.99.1: PID du daemon, lu par l'app apres redemarrage
    QString m_controlFile;
    QString m_statsFile;
    QString m_doneFile;
    QString m_tracksDir;
    QString m_partFile;
    QString m_manualStartFile;
    QString m_confFile;
};

#endif // GPSDAEMON_H
