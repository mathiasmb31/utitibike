import QtQuick 2.12
import Ubuntu.Components 1.3
import "."

// Bouton rond flottant de la carte (colonne de droite) : dégradé
// orange→aubergine + ombre simulée (rectangle décalé derrière, pas de
// DropShadow fiable sur UT arm64) — même look que le FAB principal.
Item {
    id: root
    implicitWidth: units.gu(6)
    implicitHeight: implicitWidth
    default property alias content: inner.data
    signal clicked()

    Rectangle {
        x: 0
        y: units.gu(0.25)
        width: parent.width
        height: parent.height
        radius: width / 2
        color: Theme.shadowColor
        opacity: 0.4
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.accentOrange }
            GradientStop { position: 1.0; color: Theme.ubuntuAubergine }
        }
        Item {
            id: inner
            anchors.fill: parent
        }
        MouseArea {
            anchors.fill: parent
            onClicked: root.clicked()
        }
    }
}
