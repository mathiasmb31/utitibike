import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import Utitibike 1.0
import "components"

Page {
    id: historyPage
    property string filterText: ""
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

    // 0 = date décroissante, 1 = distance décroissante
    property int sortMode: 0

    // Le modèle trié garde l'index d'ORIGINE (idx) : c'est lui qui sert à
    // gpxAt()/profileAt(), sinon on chargerait la mauvaise trace.
    function sortedEntries() {
        var out = []
        var src = routeHistory.entries
        for (var i = 0; i < src.length; i++)
            out.push({ e: src[i], idx: i })
        out.sort(function(a, b) {
            if (historyPage.sortMode === 1)
                return (b.e.distanceM || 0) - (a.e.distanceM || 0)
            return String(b.e.date || "").localeCompare(String(a.e.date || ""))
        })
        return out
    }

    function loadEntry(idx) {
        var gpx = routeHistory.gpxAt(idx)
        console.log("[history] loadEntry index=", idx, "gpx chars=", gpx.length)
        if (gpx !== "" && gpx.length > 100) {
            // brouter est une context property (main.cpp),
            // accessible directement depuis HistoryPage.
            brouter.loadingFromHistory = true
            brouter.loadGpxData(gpx)
            pageStack.pop()
        } else {
            console.warn("[history] loadEntry: GPX vide ou trop court, rien charge")
            toastLabel.text = i18n.tr("Itinéraire sans tracé GPX")
            toastRect.opacity = 1
            toastTimer.restart()
        }
    }

    Rectangle {
        id: toastRect
        z: 50
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: units.gu(2)
        }
        width: toastLabel.width + units.gu(2)
        height: toastLabel.height + units.gu(1)
        radius: Theme.radiusChip
        color: Theme.bgCard
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: Theme.textPrimary
            fontSize: "small"
        }
    }
    Timer {
        id: toastTimer
        interval: 2800
        onTriggered: toastRect.opacity = 0
    }

    // Confirmation avant « Tout effacer » (suppression définitive).
    Dialog {
        id: clearDialog
        title: i18n.tr("Tout effacer ?")
        text: i18n.tr("Supprimer définitivement tous les itinéraires de l'historique ?")
        Button {
            height: units.gu(6)
            Accessible.name: text
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
            Accessible.name: text
            onClicked: PopupUtils.close(clearDialog)
        }
    }

    ListView {
        id: historyListView
        anchors.fill: parent
        model: sortedEntries()
        spacing: 0   // les dividers des ListItem gèrent la séparation
        header: Item {
            // v0.99.1: l'ancien header etait un ListItem avec
            // `height: headerCol.height + gu(2)` alors que headerCol etait
            // anchors.centerIn: parent — binding circulaire qui laissait
            // l'en-tete trop court : son contenu (filtre + bouton) debordait
            // sur la 1re ligne de la liste (impossible a taper).
            // Column ancre en haut, hauteur autonome : plus de chevauchement.
            height: headerCol.height + units.gu(1)
            Column {
                id: headerCol
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    topMargin: units.gu(0.5)
                    leftMargin: units.gu(1)
                    rightMargin: units.gu(1)
                }
                spacing: units.gu(0.5)
                // v0.99.1: recherche/filtre par nom de parcours.
                TextField {
                    id: historyFilter
                    width: parent.width
                    placeholderText: i18n.tr("Filtrer les parcours…")
                    Accessible.name: placeholderText
                    onTextChanged: {
                        historyPage.filterText = text
                        historyListView.forceLayout()
                    }
                }
                // v0.99.1: les gestes n'etaient pas discoverables.
                Label {
                    width: parent.width
                    text: i18n.tr("Glissez → charger · ← supprimer")
                    fontSize: "x-small"
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    visible: routeHistory.entries.length > 0
                }
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: i18n.tr("Tri")
                        fontSize: "small"
                        color: Theme.textSecondary
                    }
                    SegmentedControl {
                        width: parent.width - units.gu(6)
                        height: units.gu(5)
                        model: [
                            { label: i18n.tr("Date"), value: 0 },
                            { label: i18n.tr("Distance"), value: 1 }
                        ]
                        currentIndex: historyPage.sortMode
                        onSelected: {
                            historyPage.sortMode = value
                            historyListView.forceLayout()
                        }
                    }
                }
                Row {
                    id: headerRow
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
                        Accessible.name: text
                        iconName: "delete"
                        color: Theme.accentRed
                        visible: routeHistory.entries.length > 0
                        onClicked: PopupUtils.open(clearDialog)
                    }
                }
            }  // Column headerCol
        }
        // ListItem natif Ubuntu Touch : divider, highlight au tap, swipe
        // gauche = supprimer, swipe droite = charger sur la carte.
        // (Lomiri Components 1.3 : ListItemActions contient des Action,
        //  assignes par id — ListItemAction n'existe pas.)
        delegate: ListItem {
            id: entryItem
            // v0.99.1: filtre par nom (masquage simple, modele inchange).
            visible: historyPage.filterText === ""
                     || (modelData.e.name || "").toLowerCase()
                        .indexOf(historyPage.filterText.toLowerCase()) >= 0
            height: visible ? entryCol.height + units.gu(2.4) : 0
            ListItemActions {
                id: deleteActions
                Action {
                    iconName: "delete"
                    text: i18n.tr("Supprimer")
                    onTriggered: routeHistory.removeEntry(modelData.idx)
                }
            }
            ListItemActions {
                id: loadActions
                Action {
                    iconName: "document-open"
                    text: i18n.tr("Charger")
                    onTriggered: historyPage.loadEntry(modelData.idx)
                }
            }
            leadingActions: deleteActions
            trailingActions: loadActions
            onClicked: historyPage.loadEntry(modelData.idx)

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
                    text: modelData.e.name
                    fontSize: "medium"
                    font.bold: true
                    color: Theme.textPrimary
                }
                Label {
                    fontSize: "small"
                    color: Theme.textSecondary
                    text: Format.date(modelData.e.date)
                          + "  ·  " + Format.num(modelData.e.distanceM / 1000) + " km"
                          + "  ·  D+ " + Math.round(modelData.e.ascentM) + " m"
                }
                // Mini profil d'altitude du parcours (depuis le GPX)
                ElevationProfile {
                    width: parent.width
                    height: units.gu(6)
                    points: routeHistory.profileAt(modelData.idx)
                    visible: points.length > 1
                    // v0.99.1: le MouseArea de scrubbing du profil (ajoute pour
                    // la carte de navigation) couvrait tout le mini-profil et
                    // avalait le tap : le ListItem ne recevait plus onClicked
                    // et la trace ne se chargeait pas. Ici le profil est purement
                    // decoratif — on desactive ses entrees, le Canvas continue
                    // de peindre normalement.
                    enabled: false
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
