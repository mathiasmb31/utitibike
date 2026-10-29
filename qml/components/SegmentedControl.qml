import QtQuick 2.12
import Ubuntu.Components 1.3
import "."

// Sélecteur « pilules » horizontales — remplace les Row de Button
// (profil BRouter, dénivelé, style de carte).
//
// Contrat : le parent bind currentIndex sur sa source de vérité et met à
// jour celle-ci dans onSelected. Le composant ne s'assigne PAS currentIndex
// lui-même : une assignation JS casserait le binding du parent (piège QML).
Item {
    id: root
    property var model: []          // [{ label: "...", value: "..." }]
    property int currentIndex: 0
    property color activeColor: Theme.accentOrange
    signal selected(int index, var value)

    implicitHeight: Theme.chipHeight

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSection
        radius: height / 2
    }

    Row {
        anchors.fill: parent
        anchors.margins: units.gu(0.3)
        spacing: 0

        Repeater {
            model: root.model
            Rectangle {
                width: (root.width - units.gu(0.6)) / Math.max(1, root.model.length)
                height: parent.height
                radius: height / 2
                color: root.currentIndex === index ? root.activeColor : "transparent"
                Behavior on color { ColorAnimation { duration: 150 } }

                Label {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.currentIndex === index ? Theme.textOnAccent : Theme.textSecondary
                    fontSize: "small"
                    font.bold: root.currentIndex === index
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selected(index, modelData.value)
                }
            }
        }
    }
}
