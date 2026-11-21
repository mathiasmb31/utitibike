#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
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

    // v0.99.1: le daemon aussi doit parler la langue du systeme — sinon ses
    // notifications (demarrage, batterie, disque plein, reprise) restent en
    // francais alors que l'app est traduite en 14 langues.
    QTranslator translator;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString sysLocale = QLocale::system().name();
    const QString shortLocale = sysLocale.section(QLatin1Char('_'), 0, 0);
    if (translator.load(QStringLiteral("utitibike_") + sysLocale, appDir)
            || translator.load(QStringLiteral("utitibike_") + shortLocale, appDir)) {
        app.installTranslator(&translator);
        qWarning() << "[gpsd] translation loaded for locale" << sysLocale;
    }

    GpsDaemon daemon;
    daemon.run();

    return app.exec();
}
