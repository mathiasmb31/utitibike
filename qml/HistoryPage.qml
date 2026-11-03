import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import Utitibike 1.0
import "components"

Page {
    id: historyPage
    header: AppHeader {
        title: i18n.tr("Historique")
        trailingActionBar.actions: [
            Action {
                text: i18n.tr("Stats")
                iconName: "view-list-symbolic"
                onTriggered: pageStack.push(Qt.resolvedUrl("StatsPage.qml"))
            }
        ]
    }

    // Fond de page (la Page a un fond blanc par défaut)
    Rectangle {
        anchors.fill: parent
        color: Theme.bgPage
        z: -10
    }

    function loadEntry(idx) {
        var gpx = routeHistory.gpxAt(idx)
        if (gpx !== "" && gpx.length > 100) {
            // brouter est une context property (main.cpp),
            // accessible directement depuis HistoryPage.
            brouter.loadingFromHistory = true
            brouter.loadGpxData(gpx)
            pageStack.pop()
        }
    }

    // Confirmation avant « Tout effacer » (suppression définitive).
    Dialog {
        id: clearDialog
        title: i18n.tr("Tout effacer ?")
        text: i18n.tr("Supprimer définitivement tous les itinéraires de l'historique ?")
        Button {
            height: units.gu(6)
            text: i18n.tr("Tout effacer")
            color: Theme.accentRed
            onClicked: {
                routeHistory.clear()
                PopupUtils.close(clearDialog)
            }
        }
        Button {
            height: units.gu(6)
            text: i18n.tr("Annuler")
            onClicked: PopupUtils.close(clearDialog)
        }
    }

    ListView {
        anchors.fill: parent
        model: routeHistory.entries
        spacing: 0   // les dividers des ListItem gèrent la séparation
        header: ListItem {
            height: headerRow.height + units.gu(2)
            StyleHints {
                backgroundColor: Theme.bgCard
                dividerColor: Theme.bgCardBorder
            }
            Row {
                id: headerRow
                anchors.centerIn: parent
                spacing: units.gu(2)
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18n.tr("%1 itinéraire(s)").arg(routeHistory.entries.length)
                    fontSize: "small"
                    color: Theme.textPrimary
                }
                Button {
                    height: units.gu(6)
                    text: i18n.tr("Tout effacer")
                    iconName: "delete"
                    color: Theme.accentRed
                    visible: routeHistory.entries.length > 0
                    onClicked: PopupUtils.open(clearDialog)
                }
            }
        }
        // ListItem natif Ubuntu Touch : divider, highlight au tap, swipe
        // gauche = supprimer, swipe droite = charger sur la carte.
        delegate: ListItem {
            height: entryCol.height + units.gu(2.4)
            StyleHints {
                backgroundColor: Theme.bgCard
                selectedBackgroundColor: Theme.bgSection
                dividerColor: Theme.bgCardBorder
                foregroundColor: Theme.textPrimary
            }
            leadingActions: ListItemActions {
                ListItemAction {
                    iconName: "delete"
                    text: i18n.tr("Supprimer")
                    onTriggered: routeHistory.removeEntry(index)
                }
            }
            trailingActions: ListItemActions {
                ListItemAction {
                    iconName: "document-open"
                    text: i18n.tr("Charger")
                    onTriggered: historyPage.loadEntry(index)
                }
            }
            onClicked: historyPage.loadEntry(index)

            Column {
                id: entryCol
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: units.gu(2)
                    rightMargin: units.gu(2)
                }
                spacing: units.gu(0.3)
                Label {
                    width: parent.width
                    elide: Text.ElideRight
                    text: modelData.name
                    fontSize: "medium"
                    font.bold: true
                    color: Theme.textPrimary
                }
                Label {
                    fontSize: "small"
                    color: Theme.textSecondary
                    text: new Date(modelData.date).toLocaleDateString()
                          + "  ·  " + (modelData.distanceM / 1000).toFixed(1) + " km"
                          + "  ·  D+ " + Math.round(modelData.ascentM) + " m"
                }
                // Mini profil d'altitude du parcours (depuis le GPX)
                ElevationProfile {
                    width: parent.width
                    height: units.gu(5)
                    points: routeHistory.profileAt(index)
                    visible: points.length > 1
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: routeHistory.entries.length === 0
        text: i18n.tr("Aucun itinéraire dans l'historique")
        color: Theme.textMuted
    }
}
