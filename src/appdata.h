#ifndef APPDATA_H
#define APPDATA_H

#include <QString>

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
    return g_appDataDir;
}

#endif // APPDATA_H
