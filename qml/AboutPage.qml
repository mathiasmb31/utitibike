import QtQuick 2.12
import Ubuntu.Components 1.3
import "components"

Page {
    id: aboutPage
    title: i18n.tr("À propos")

    // Fond sombre (la Page a un fond blanc par défaut)
    Rectangle {
        anchors.fill: parent
        color: Theme.bgPage
        z: -10
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: units.gu(2)
        contentHeight: aboutCol.height

        Column {
            id: aboutCol
            width: parent.width
            spacing: units.gu(2)

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: units.gu(12)
                height: units.gu(12)
                source: "../logo.png"
                fillMode: Image.PreserveAspectFit
            }

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "Utitibike"
                fontSize: "large"
                font.bold: true
                color: Theme.accentOrange
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: i18n.tr("Version %1").arg(appVersion)
                color: Theme.textSecondary
            }

            // Badge « made for Ubuntu Touch » : pastille dégradé orange
            // Ubuntu → aubergine + logo Ubuntu du thème d'icônes (suru).
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: badgeRow.width + units.gu(4)
                height: units.gu(5)
                radius: height / 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.accentOrange }
                    GradientStop { position: 1.0; color: Theme.ubuntuAubergine }
                }
                Row {
                    id: badgeRow
                    anchors.centerIn: parent
                    spacing: units.gu(1)
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "ubuntu-logo-symbolic"
                        width: units.gu(3)
                        height: units.gu(3)
                        color: "#ffffff"
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: i18n.tr("Conçu pour Ubuntu Touch")
                        color: "#ffffff"
                        fontSize: "small"
                        font.bold: true
                    }
                }
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                fontSize: "small"
                color: Theme.textSecondary
                text: i18n.tr("Ubuntu Touch 24.04 · framework ubuntu-touch-24.04-1.x · Qt 5.15")
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textPrimary
                text: i18n.tr("Itinéraires vélo : carte OpenStreetMap hors ligne, calcul BRouter et export GPX.")
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: units.gu(0.1); color: Theme.bgCardBorder }

            Label {
                text: i18n.tr("Crédits")
                fontSize: "medium"
                font.bold: true
                color: Theme.textPrimary
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                fontSize: "small"
                color: Theme.textSecondary
                text: "© OpenStreetMap contributors (ODbL)\n"
                    + "BRouter — abrensch/brouter (AGPL)\n"
                    + "Nominatim — nominatim.openstreetmap.org\n"
                    + "Tuiles CyclOSM — cyclOSM\n"
                    + "Tuiles sombre — CARTO\n"
                    + "Tuiles satellite — Esri"
            }

            Rectangle { width: parent.width; height: units.gu(0.1); color: Theme.bgCardBorder }

            Label {
                text: i18n.tr("Astuces")
                fontSize: "medium"
                font.bold: true
                color: Theme.textPrimary
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                fontSize: "small"
                color: Theme.textSecondary
                text: i18n.tr("• Appui long sur la carte : ajouter un point\n"
                            + "• Glisse : déplacer la carte\n"
                            + "• Pince : zoomer\n"
                            + "• Bouton orange : panneau itinéraire\n"
                            + "• Icône recherche : trouver un lieu\n"
                            + "• GPS : position en temps réel + navigation")
            }
        }
    }
}
