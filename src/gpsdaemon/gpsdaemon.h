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

// Daemon GPS détaché pour utitibike. S'exécute sans UI (QCoreApplication),
// enregistre les positions GPS dans un fichier GPX, écrit un JSON de stats
// live, et émet un beep toutes les 10 s. Lancé par l'app QML via
// QProcess::startDetached() — échappe au SIGSTOP de Lomiri (le gel
// cible le processus principal de l'app, pas les fils détachés).
//
// Fichiers dans appDataDir (~/.local/share/mathias.utitibike/) :
//   recording.control  — créé par l'app pour démarrer, supprimé = arrêt
//   recording.json     — stats live (distance, D+, durée, vitesse, nb pts)
//   recording.done     — résumé final après arrêt (nom, distance, D+, GPX)
//   tracks/en-cours.gpx — auto-sauvegarde atomique toutes les 60 s
class GpsDaemon : public QObject
{
    Q_OBJECT
public:
    explicit GpsDaemon(QObject *parent = nullptr);
    ~GpsDaemon();

    void run();

private slots:
    void onPositionUpdated(const QGeoPositionInfo &info);
    void onAutoSave();
    void onStatsTick();
    void onBeepTick();
    void onControlTick();
    void cleanup();  // slot : appelé via QMetaObject::invokeMethod depuis le handler SIGTERM

private:
    void writeStatsJson();
    void writeDoneJson(const QString &gpxPath);
    QString generateGpx() const;
    QString saveGpx(const QString &fileName) const;

    QGeoPositionInfoSource *m_gps = nullptr;
    QTimer m_autoSaveTimer;    // 60 s — sauvegarde GPX périodique
    QTimer m_statsTimer;       // 2 s — écriture du JSON de stats
    QTimer m_beepTimer;        // 10 s — beep de confirmation
    QTimer m_controlTimer;     // 2 s — vérifie si recording.control existe
    QTimer m_fakeGpsTimer;      // DEBUG — GPS simulé (UTITIBIKE_FAKE_GPS=1)
    QSoundEffect m_beep;

    bool m_recording = false;
    int m_parentPid = 0;       // PID de l'app QML (lu depuis recording.control)
    QVariantList m_points;
    double m_distance = 0.0;
    double m_ascent = 0.0;
    QElapsedTimer m_elapsed;
    QDateTime m_startTime;
    double m_lastLat = 0.0;
    double m_lastLon = 0.0;
    double m_lastAlt = 0.0;
    bool m_hasAlt = false;
    bool m_hasLast = false;

    QString m_appDataDir;
    QString m_controlFile;
    QString m_statsFile;
    QString m_doneFile;
    QString m_tracksDir;
};

#endif // GPSDAEMON_H
