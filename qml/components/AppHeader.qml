import QtQuick 2.12
import Ubuntu.Components 1.3

// PageHeader au look Ubuntu Touch : fond « Ubuntu dark » #2C001E en sombre,
// blanc en clair, séparateur discret — au lieu du header par défaut.
// customColors + bgColor/fgColor : la carte principale colore son bandeau
// selon le mode (gris clair vélo / terre randonnée).
PageHeader {
    id: root
    property bool customColors: false
    property color bgColor: Theme.headerBg
    property color fgColor: Theme.headerFg

    StyleHints {
        backgroundColor: root.customColors ? root.bgColor : Theme.headerBg
        foregroundColor: root.customColors ? root.fgColor : Theme.headerFg
        dividerColor: Theme.headerDivider
    }
}
