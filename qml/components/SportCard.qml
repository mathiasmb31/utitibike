import QtQuick 2.12
import "."

// Carte sombre « Urban Sport » : fond #1a1a2e, bordure subtile blanche à 8 %,
// coins arrondis. Remplace les Rectangle blancs/gris des panneaux.
// L'import "." résout le singleton Theme depuis le même répertoire
// (testé dans le qrc : les composants internes voient Theme).
Rectangle {
    id: root
    // Marge intérieure (dp) — par défaut 1,5 gu ; les chips (1 ligne)
    // passent padding: units.gu(0.5) pour rester compacts.
    property real padding: Theme.padCard

    color: Theme.bgCard
    radius: Theme.radiusCard
    border.color: Theme.bgCardBorder
    border.width: 1
    default property alias content: inner.data

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
