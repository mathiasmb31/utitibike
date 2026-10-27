#ifndef APPDATA_H
#define APPDATA_H

#include <QString>
#include <QStandardPaths>

// Sur Ubuntu Touch, le plugin QPA ubuntumirclient écrase applicationName
// (défini à "mathias.utitibike" avant QGuiApplication) avec le nom du
// desktop ("utitibike"). QStandardPaths::AppDataLocation retourne alors
// ~/.local/share/utitibike — HORS du template AppArmor par défaut qui ne
// couvre que ~/.local/share/@{APP_PKGNAME}/** (mathias.utitibike).
//
// main() sauvegarde le chemin correct AVANT la création du QML engine
// (qui instancie TileCache et autres) dans g_appDataDir.
// Tous les composants doivent utiliser appDataDir() au lieu de
// QStandardPaths::AppDataLocation.

extern QString g_appDataDir;

inline QString appDataDir()
{
    // g_appDataDir est peuplé par main() avant la création du QML engine.
    // Dans les tests unitaires (pas de main()), il reste vide : on retombe
    // alors sur QStandardPaths — identique à l'ancien comportement, y compris
    // en test mode (QStandardPaths::setTestModeEnabled).
    if (g_appDataDir.isEmpty())
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return g_appDataDir;
}

#endif // APPDATA_H
