import QtQuick 2.12
import Ubuntu.Components 1.3
import Utitibike 1.0
import "components"

// Statistiques cumulées calculées depuis l'historique (history.json) :
// totaux, semaine en cours, mois en cours, record de distance.
Page {
    id: statsPage
    header: AppHeader { title: i18n.tr("Statistiques") }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgPage
        z: -10
    }

    property int totalCount: 0
    property real totalKm: 0
    property real totalAscent: 0
    property int weekCount: 0
    property real weekKm: 0
    property real weekAscent: 0
    property int monthCount: 0
    property real monthKm: 0
    property real monthAscent: 0
    property real recordKm: 0

    function mondayOf(d) {
        var m = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        m.setDate(m.getDate() - ((m.getDay() + 6) % 7))
        return m
    }
    function firstOfMonth(d) {
        return new Date(d.getFullYear(), d.getMonth(), 1)
    }

    function recompute() {
        var now = new Date()
        var week0 = mondayOf(now).getTime()
        var month0 = firstOfMonth(now).getTime()
        statsPage.totalCount = 0; statsPage.totalKm = 0; statsPage.totalAscent = 0
        statsPage.weekCount = 0; statsPage.weekKm = 0; statsPage.weekAscent = 0
        statsPage.monthCount = 0; statsPage.monthKm = 0; statsPage.monthAscent = 0
        statsPage.recordKm = 0
        var entries = routeHistory.entries
        for (var i = 0; i < entries.length; i++) {
            var e = entries[i]
            var km = e.distanceM / 1000
            var asc = e.ascentM
            var t = new Date(e.date).getTime()
            statsPage.totalCount++
            statsPage.totalKm += km
            statsPage.totalAscent += asc
            if (km > statsPage.recordKm)
                statsPage.recordKm = km
            if (t >= week0) {
                statsPage.weekCount++
                statsPage.weekKm += km
                statsPage.weekAscent += asc
            }
            if (t >= month0) {
                statsPage.monthCount++
                statsPage.monthKm += km
                statsPage.monthAscent += asc
            }
        }
    }

    Component.onCompleted: recompute()
    Connections {
        target: routeHistory
        function onEntriesChanged() { statsPage.recompute() }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: statsCol.height + units.gu(4)
        Column {
            id: statsCol
            width: parent.width - units.gu(4)
            anchors.horizontalCenter: parent.horizontalCenter
            y: units.gu(2)
            spacing: units.gu(1.5)

            Label {
                text: i18n.tr("Depuis le début")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: totalRow.height + units.gu(2)
                padding: units.gu(1)
                Row {
                    id: totalRow
                    anchors.centerIn: parent
                    spacing: units.gu(3)
                    Column {
                        Label { text: statsPage.totalKm.toFixed(1) + " km"; fontSize: "large"; font.bold: true; color: Theme.accentOrange }
                        Label { text: i18n.tr("Distance"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: Math.round(statsPage.totalAscent) + " m"; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("D+ total"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: "" + statsPage.totalCount; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("Sorties"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                }
            }

            Label {
                text: i18n.tr("Cette semaine")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: weekRow.height + units.gu(2)
                padding: units.gu(1)
                Row {
                    id: weekRow
                    anchors.centerIn: parent
                    spacing: units.gu(3)
                    Column {
                        Label { text: statsPage.weekKm.toFixed(1) + " km"; fontSize: "large"; font.bold: true; color: Theme.accentOrange }
                        Label { text: i18n.tr("Distance"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: Math.round(statsPage.weekAscent) + " m"; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("D+"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: "" + statsPage.weekCount; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("Sorties"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                }
            }

            Label {
                text: i18n.tr("Ce mois-ci")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: monthRow.height + units.gu(2)
                padding: units.gu(1)
                Row {
                    id: monthRow
                    anchors.centerIn: parent
                    spacing: units.gu(3)
                    Column {
                        Label { text: statsPage.monthKm.toFixed(1) + " km"; fontSize: "large"; font.bold: true; color: Theme.accentOrange }
                        Label { text: i18n.tr("Distance"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: Math.round(statsPage.monthAscent) + " m"; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("D+"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                    Column {
                        Label { text: "" + statsPage.monthCount; fontSize: "large"; font.bold: true; color: Theme.textPrimary }
                        Label { text: i18n.tr("Sorties"); fontSize: "x-small"; color: Theme.textSecondary }
                    }
                }
            }

            Label {
                text: i18n.tr("Record de distance")
                fontSize: "small"
                font.bold: true
                color: Theme.textSecondary
            }
            SportCard {
                width: parent.width
                height: units.gu(7)
                padding: units.gu(1)
                Label {
                    anchors.centerIn: parent
                    text: statsPage.recordKm.toFixed(1) + " km"
                    fontSize: "x-large"
                    font.bold: true
                    color: Theme.accentOrange
                }
            }
        }
    }
}
