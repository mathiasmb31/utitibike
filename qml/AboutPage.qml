import QtQuick 2.12
import Ubuntu.Components 1.3

Page {
    id: aboutPage
    title: i18n.tr("À propos")

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
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: i18n.tr("Version %1").arg(appVersion)
                color: "#666"
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: i18n.tr("Itinéraires vélo : carte OpenStreetMap hors ligne, calcul BRouter et export GPX.")
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: units.gu(0.1); color: "#d2d2d2" }

            Label {
                text: i18n.tr("Crédits")
                fontSize: "medium"
                font.bold: true
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                fontSize: "small"
                text: "© OpenStreetMap contributors (ODbL)\n"
                    + "BRouter — abrensch/brouter (AGPL)\n"
                    + "Nominatim — nominatim.openstreetmap.org\n"
                    + "Tuiles CyclOSM — cyclOSM\n"
                    + "Tuiles sombre — CARTO\n"
                    + "Tuiles satellite — Esri"
            }

            Rectangle { width: parent.width; height: units.gu(0.1); color: "#d2d2d2" }

            Label {
                text: i18n.tr("Astuces")
                fontSize: "medium"
                font.bold: true
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                fontSize: "small"
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
