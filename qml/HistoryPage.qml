import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import Utitibike 1.0

Page {
    id: historyPage
    title: i18n.tr("Historique")

    // Confirmation avant « Tout effacer » (suppression définitive).
    // Ubuntu.Components.Popups 1.3 : les types s'utilisent sans préfixe.
    Dialog {
        id: clearDialog
        title: i18n.tr("Tout effacer ?")
        text: i18n.tr("Supprimer définitivement tous les itinéraires de l'historique ?")
        Button {
            text: i18n.tr("Tout effacer")
            color: UbuntuColors.red
            onClicked: {
                routeHistory.clear()
                PopupUtils.close(clearDialog)
            }
        }
        Button {
            text: i18n.tr("Annuler")
            onClicked: PopupUtils.close(clearDialog)
        }
    }

    ListView {
        anchors.fill: parent
        model: routeHistory.entries
        spacing: units.gu(0.3)
        header: Rectangle {
            width: parent.width
            height: headerCol.height + units.gu(2)
            color: "#f7f7f7"
            Column {
                id: headerCol
                anchors.centerIn: parent
                Label {
                    text: i18n.tr("%1 itinéraire(s)").arg(routeHistory.entries.length)
                    fontSize: "small"
                }
                Button {
                    text: i18n.tr("Tout effacer")
                    color: UbuntuColors.red
                    visible: routeHistory.entries.length > 0
                    onClicked: PopupUtils.open(clearDialog)
                }
            }
        }
        delegate: Rectangle {
            width: parent.width - units.gu(2)
            height: entryCol.height + units.gu(2)
            anchors.horizontalCenter: parent.horizontalCenter
            color: "white"
            radius: units.gu(0.5)
            border.color: "#e0e0e0"

            Column {
                id: entryCol
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: units.gu(1)
                }
                spacing: units.gu(0.3)
                Label {
                    text: modelData.name
                    fontSize: "medium"
                    font.bold: true
                }
                Label {
                    fontSize: "small"
                    color: "#666"
                    text: new Date(modelData.date).toLocaleDateString()
                          + "  ·  " + (modelData.distanceM / 1000).toFixed(1) + " km"
                          + "  ·  D+ " + Math.round(modelData.ascentM) + " m"
                }
                Row {
                    spacing: units.gu(0.5)
                    Button {
                        text: i18n.tr("Charger")
                        color: UbuntuColors.blue
                        onClicked: {
                            var gpx = routeHistory.gpxAt(index)
                            if (gpx !== "" && gpx.length > 100) {
                                // brouter est une context property (main.cpp),
                                // accessible directement depuis HistoryPage.
                                brouter.loadingFromHistory = true
                                brouter.loadGpxData(gpx)
                                pageStack.pop()
                            }
                        }
                    }
                    Button {
                        text: i18n.tr("Supprimer")
                        color: UbuntuColors.red
                        onClicked: routeHistory.removeEntry(index)
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: routeHistory.entries.length === 0
        text: i18n.tr("Aucun itinéraire dans l'historique")
        color: "#999"
    }
}
