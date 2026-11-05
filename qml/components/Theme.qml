pragma Singleton
import QtQuick 2.12

// Thème « Urban Sport » — singleton de design de l'app.
// Importé depuis les pages par : import "components" (qml/components/,
// enregistré dans qmldir + qml.qrc — testé : l'import par URI
// « Utitibike.Components » ne résout pas depuis un qrc sans addImportPath).
//
// Le singleton est instancié dans le contexte racine de l'engine : on
// n'utilise ni units.gu() (indisponible hors MainView) ni Ubuntu.Components.
// Les dimensions sont en dp (1 gu Ubuntu Touch = 8 dp).
//
// Thème clair/sombre : mode "auto" suit le thème système Lomiri (palette
// propagée par le QPA, lue via le context property paletteWatcher),
// "dark"/"light" forcent. Le mode est persisté dans viewState.themeMode
// et bindé sur Theme.mode depuis Main.qml.
QtObject {
    property string mode: "auto"
    readonly property bool systemDark: typeof paletteWatcher !== "undefined"
                                       ? paletteWatcher.dark : true
    readonly property bool dark: mode === "dark" || (mode === "auto" && systemDark)

    // Fonds
    readonly property color bgCard: dark ? "#1a1a2e" : "#ffffff"
    // Bordure subtile : blanc à 8 % en sombre, noir à 8 % en clair —
    // littéral #AARRGGBB (alpha d'abord), PAS "#ffffff15" que Qt lirait
    // comme du jaune opaque (#ffff15).
    readonly property color bgCardBorder: dark ? "#15ffffff" : "#15000000"
    // Fond de page : « Ubuntu dark » #2C001E en sombre (même couleur que
    // X-Ubuntu-Splash-Color dans utitibike.desktop et le splash in-app),
    // blanc cassé chaud en clair.
    readonly property color bgPage: dark ? "#2C001E" : "#F8F5F2"
    readonly property color bgSection: dark ? "#252540" : "#ececf1"
    readonly property color bgInput: dark ? "#1e1e38" : "#ffffff"

    // Texte
    readonly property color textPrimary: dark ? "#e8e8e8" : "#2C001E"
    readonly property color textSecondary: dark ? "#888899" : "#6d6d7c"
    readonly property color textOnAccent: "#ffffff"
    readonly property color textMuted: dark ? "#555566" : "#9a9aa5"

    // En-tête des pages (AppHeader) : fond sombre Ubuntu / blanc, texte
    // inversé, séparateur discret.
    readonly property color headerBg: dark ? ubuntuDark : "#ffffff"
    readonly property color headerFg: dark ? "#ffffff" : "#2C001E"
    readonly property color headerDivider: dark ? "#33ffffff" : "#33000000"

    // Bandeau de mode sur la carte principale : gris clair en mode vélo,
    // terre en mode randonnée (marche) — avec le texte/pictogramme adapté.
    readonly property color headerModeBike: "#E4E4E4"
    readonly property color headerModeBikeFg: "#2C001E"
    readonly property color headerModeFoot: "#8A6642"
    readonly property color headerModeFootFg: "#ffffff"

    // Accents — identiques en clair et sombre
    // Orange Ubuntu officiel (#E95420) + aubergine Ubuntu (#772953) :
    // le duo de couleurs de la marque Ubuntu Touch.
    readonly property color accentOrange: "#E95420"
    readonly property color accentAmber: "#f7931e"
    readonly property color ubuntuAubergine: "#772953"   // aubergine Ubuntu
    readonly property color ubuntuDark: "#2C001E"        // fond sombre Ubuntu
    readonly property color accentCyan: "#00d4ff"
    readonly property color accentGreen: "#2ecc71"
    readonly property color accentRed: "#e74c3c"
    readonly property color accentBlue: "#3498db"

    // Sémantique difficulté
    readonly property color diffFlat: "#2ecc71"      // Plat
    readonly property color diffRolling: "#8bc34a"   // Vallonné
    readonly property color diffSporty: "#f39c12"    // Sportif
    readonly property color diffMountain: "#e74c3c"  // Montagne

    // Rayons (dp — 1 gu = 8 dp)
    readonly property real radiusCard: 12       // 1,5 gu
    readonly property real radiusChip: 16       // 2 gu
    readonly property real radiusButton: 6      // 0,8 gu
    readonly property real radiusFab: 28        // 3,5 gu

    // Espacements (dp)
    readonly property real padCard: 12          // 1,5 gu
    readonly property real padSection: 8        // 1 gu

    // Hauteurs (px — les valeurs brutes QML sont en pixels, pas en dp)
    readonly property real chipHeight: 56       // pilules SegmentedControl (~2,7 gu)
    readonly property real fabSize: 144         // FAB (~7 gu, taille Material 56 dp)
    readonly property real fabIconSize: 72      // icône FAB (~3,5 gu)

    // Ombre simulée (pas de layer.effect DropShadow fiable sur UT arm64)
    readonly property color shadowColor: "#000000"
}
