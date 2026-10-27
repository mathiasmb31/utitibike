#include "appdata.h"

// Définition du global partagé (déclaration dans appdata.h).
// Séparée de main.cpp pour que les tests unitaires — qui compilent les
// sources de src/ mais ne linkent pas main.cpp — résolvent le symbole.
QString g_appDataDir;
