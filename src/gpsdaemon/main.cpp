#include <QCoreApplication>
#include "gpsdaemon.h"

// Point d'entrée du daemon GPS utitibike.
// Lancé par l'app QML via QProcess::startDetached("utitibike-gpsd").
// S'exécute sans UI, enregistre le GPS, écrit les stats en JSON.
int main(int argc, char *argv[])
{
    // Même applicationName que l'app principale pour partager appDataDir
    // (~/.local/share/mathias.utitibike/).
    QCoreApplication::setApplicationName(QStringLiteral("mathias.utitibike"));
    QCoreApplication::setOrganizationName(QStringLiteral(""));

    QCoreApplication app(argc, argv);

    GpsDaemon daemon;
    daemon.run();

    return app.exec();
}
