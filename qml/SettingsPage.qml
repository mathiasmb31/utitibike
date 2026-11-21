import QtQuick 2.12
import Ubuntu.Components 1.3
import "components"

// v0.99.1: page de reglages dediee — accessible depuis le panneau de la carte.
// N'utilise QUE des context properties (viewState, brouter, daemonController,
// haptics, urlOpener) : aucun couplage avec les proprietes internes de MapPage.
Page {
    id: settingsPage
    header: AppHeader {
        title: i18n.tr("Réglages")
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + units.gu(4)
        Column {
            id: col
            width: parent.width - units.gu(4)
            anchors.horizontalCenter: parent.horizontalCenter
            y: units.gu(2)
            spacing: units.gu(1.5)

            Label {
                text: i18n.tr("Apparence")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: themeRow.height + units.gu(2)
                padding: units.gu(1)
                Column {
                    id: themeRow
                    anchors.centerIn: parent
                    width: parent.width - units.gu(2)
                    spacing: units.gu(1)
                    Label { text: i18n.tr("Thème"); fontSize: "small"; color: Theme.textPrimary }
                    SegmentedControl {
                        width: parent.width
                        height: units.gu(6)
                        model: [
                            { label: i18n.tr("Auto"), value: "auto" },
                            { label: i18n.tr("Sombre"), value: "dark" },
                            { label: i18n.tr("Clair"), value: "light" }
                        ]
                        currentIndex: ["auto", "dark", "light"].indexOf(viewState.themeMode)
                        onSelected: {
                            haptics.vibrate(10)
                            viewState.themeMode = value
                        }
                    }
                }
            }

            Label {
                text: i18n.tr("Navigation")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: navRow.height + units.gu(2)
                padding: units.gu(1)
                Column {
                    id: navRow
                    anchors.centerIn: parent
                    width: parent.width - units.gu(2)
                    spacing: units.gu(1)
                    Label { text: i18n.tr("Source GPS"); fontSize: "small"; color: Theme.textPrimary }
                    SegmentedControl {
                        width: parent.width
                        height: units.gu(6)
                        model: [
                            { label: i18n.tr("Auto"), value: "auto" },
                            { label: i18n.tr("Satellite"), value: "satellite" },
                            { label: i18n.tr("Réseau"), value: "network" }
                        ]
                        currentIndex: ["auto", "satellite", "network"].indexOf(viewState.positioningMode)
                        onSelected: {
                            haptics.vibrate(10)
                            viewState.positioningMode = value
                            gpsTracker.setPositioningMode(value)
                        }
                    }
                    Label { text: i18n.tr("Mode de déplacement"); fontSize: "small"; color: Theme.textPrimary }
                    SegmentedControl {
                        width: parent.width
                        height: units.gu(6)
                        model: [
                            { label: i18n.tr("Vélo"), value: "bike" },
                            { label: i18n.tr("Marche"), value: "foot" }
                        ]
                        currentIndex: viewState.transportMode === "foot" ? 1 : 0
                        onSelected: {
                            haptics.vibrate(10)
                            viewState.transportMode = value
                        }
                    }
                    Switch {
                        width: parent.width
                        height: units.gu(6)
                        text: i18n.tr("Éviter les chemins non goudronnés")
                        checked: brouter.avoidUnpaved
                        onCheckedChanged: brouter.avoidUnpaved = checked
                    }
                }
            }

            Label {
                text: i18n.tr("Enregistrement")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: recRow.height + units.gu(2)
                padding: units.gu(1)
                Column {
                    id: recRow
                    anchors.centerIn: parent
                    width: parent.width - units.gu(2)
                    spacing: units.gu(1)
                    Row {
                        width: parent.width
                        spacing: units.gu(1)
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: i18n.tr("Poids")
                            fontSize: "small"
                            color: Theme.textPrimary
                        }
                        TextField {
                            width: units.gu(8)
                            inputMethodHints: Qt.ImhDigitsOnly
                            text: viewState.userWeightKg
                            onTextChanged: {
                                var w = parseFloat(text)
                                if (!isNaN(w) && w >= 30 && w <= 250)
                                    viewState.userWeightKg = w
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: i18n.tr("kg")
                            fontSize: "small"
                            color: Theme.textSecondary
                        }
                    }
                    Switch {
                        width: parent.width
                        height: units.gu(6)
                        text: i18n.tr("Bips GPS")
                        checked: daemonController.beepEnabled
                        onCheckedChanged: daemonController.setBeepEnabled(checked)
                    }
                }
            }

            Button {
                width: parent.width
                height: units.gu(6)
                text: i18n.tr("Aide")
                color: Theme.bgSection
                onClicked: pageStack.push(Qt.resolvedUrl("HelpPage.qml"))
            }
            Button {
                width: parent.width
                height: units.gu(6)
                text: i18n.tr("À propos")
                color: Theme.bgSection
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }
    }
}
