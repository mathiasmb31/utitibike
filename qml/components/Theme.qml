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
QtObject {
    // Fonds
    readonly property color bgCard: "#1a1a2e"
    // Bordure subtile : blanc à 8 % — littéral #AARRGGBB (alpha d'abord),
    // PAS "#ffffff15" que Qt lirait comme du jaune opaque (#ffff15).
    readonly property color bgCardBorder: "#15ffffff"
    readonly property color bgPage: "#0f0f1a"
    readonly property color bgSection: "#252540"
    readonly property color bgInput: "#1e1e38"

    // Texte
    readonly property color textPrimary: "#e8e8e8"
    readonly property color textSecondary: "#888899"
    readonly property color textOnAccent: "#ffffff"
    readonly property color textMuted: "#555566"

    // Accents
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
