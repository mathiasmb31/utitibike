
import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Content 1.3
import Utitibike 1.0
import "components"

Page {
    id: page
    title: i18n.tr("Utitibike")

    property var waypoints: []
    property var routePoints: []
    property string routeGpx: ""
    property real routeDistance: 0
    property real routeAscent: 0
    property int profileIndex: 0
    property int dlDone: 0
    property int dlTotal: 0
    property bool panelOpen: false
    property bool followGps: false
    property bool userMovedMap: false   // l'utilisateur a pan/zoomé la carte
    property bool restoringView: false  // restauration/centrage programmé en cours
    property bool searchOpen: false
    property var searchResults: []
    property bool searchBusy: false
    property bool manualStartWarned: false  // vibration déjà envoyée pour le départ manuel
    property real userWeightKg: 70  // poids pour l'estimation des calories

    // Source de position « live » unique : pendant un enregistrement, c'est le
    // daemon qui fournit la position (une seule source GPS ouverte côté app =
    // moins de batterie) ; sinon c'est le GpsTracker de l'UI.
    property bool liveRecording: daemonController.recording
    property bool liveHasFix: liveRecording ? daemonController.hasFix
                                            : (gpsTracker.active && gpsTracker.hasFix)
    property bool liveHasApprox: liveRecording ? daemonController.approx
                                               : (gpsTracker.active && gpsTracker.hasApprox)
    property bool liveHasPos: liveHasFix || liveHasApprox
    property real liveLat: liveRecording ? daemonController.latitude : gpsTracker.latitude
    property real liveLon: liveRecording ? daemonController.longitude : gpsTracker.longitude
    property real liveSpeedKmh: liveRecording ? daemonController.speedKmh : gpsTracker.speedKmh

    readonly property var profiles: ["trekking", "fastbike", "safety", "shortest"]

    // ── Estimation difficulté & calories ────────────────────────────
    // Difficulté basée sur le D+ par km (m/km de dénivelé) :
    //   < 5 m/km  → Plat      (vert)
    //   < 10 m/km → Vallonné  (vert clair)
    //   < 20 m/km → Sportif   (orange)
    //   ≥ 20 m/km → Montagne  (rouge)
    function difficultyLevel(distanceM, ascentM) {
        if (distanceM <= 0) return ""
        var mPerKm = ascentM / (distanceM / 1000)
        if (mPerKm < 5)  return i18n.tr("Plat")
        if (mPerKm < 10) return i18n.tr("Vallonné")
        if (mPerKm < 20) return i18n.tr("Sportif")
        return i18n.tr("Montagne")
    }

    function difficultyColor(distanceM, ascentM) {
        if (distanceM <= 0) return Theme.textMuted
        var mPerKm = ascentM / (distanceM / 1000)
        if (mPerKm < 5)  return Theme.diffFlat
        if (mPerKm < 10) return Theme.diffRolling
        if (mPerKm < 20) return Theme.diffSporty
        return Theme.diffMountain
    }

    // Calories estimées — formule MET (ACSM) :
    //   kcal = MET × 3.5 × poids(kg) × durée(h) × 60 / 200
    //        = MET × poids × durée × 1.05
    // Vitesse estimée : 18 km/h sur le plat, réduite de 0,8 km/h par
    // tranche de 10 m/km de D+. MET : 6 (modéré) à 10 (intense).
    function estimatedCalories(distanceM, ascentM) {
        if (distanceM <= 0) return 0
        var distKm = distanceM / 1000
        var mPerKm = ascentM / distKm
        var speed = Math.max(18 - mPerKm * 0.8, 8)  // km/h
        var durationH = distKm / speed
        var met = Math.min(6 + mPerKm * 0.15, 10)
        return Math.round(met * page.userWeightKg * durationH * 1.05)
    }

    function addWaypoint(lat, lon) {
        // Défense 1 : rejeter les coordonnées invalides (NaN, hors limites,
        // ou 0,0 qui est quasi-sûrement une erreur de conversion écran→GPS).
        if (!isFinite(lat) || !isFinite(lon)
                || lat < -90 || lat > 90
                || lon < -180 || lon > 180
                || (lat === 0 && lon === 0)) {
            showToast(i18n.tr("Position invalide — point ignoré"))
            return
        }
        // Défense 2 : un waypoint ajouté par appui long sur la carte doit
        // être dans la zone visible. Si hors champ, c'est une conversion
        // erronée (événement passé à travers un overlay, carte déplacée
        // entre le press et le timer, etc.) → point fantôme très lointain.
        var ext = map.visibleExtent()
        if (ext.lat1 !== undefined
                && (lat < ext.lat1 || lat > ext.lat2
                    || lon < ext.lon1 || lon > ext.lon2)) {
            showToast(i18n.tr("Point hors de la zone visible — ignoré"))
            return
        }
        page.waypoints = page.waypoints.concat([{ lat: lat, lon: lon }])
        page.routePoints = []
        page.routeGpx = ""
        page.routeDistance = 0
        page.routeAscent = 0
        // Coordonnées dans le toast : permet de voir si un point fantôme
        // est ajouté à (0,0) ou à des coordonnées réelles mais lointaines.
        showToast(i18n.tr("Point %1 ajouté (%2, %3)")
                  .arg(page.waypoints.length)
                  .arg(lat.toFixed(5))
                  .arg(lon.toFixed(5)))
    }

    function removeWaypoint(i) {
        var w = page.waypoints
        page.waypoints = w.slice(0, i).concat(w.slice(i + 1))
        page.routePoints = []
        page.routeGpx = ""
        page.routeDistance = 0
        page.routeAscent = 0
    }

    function exportNow() {
        var url = brouter.exportGpx(page.routeGpx)
        if (url === "") {
            showToast(i18n.tr("Échec d'écriture du GPX"))
            return
        }
        var name = url.substring(url.lastIndexOf("/") + 1)
        pageStack.push(Qt.resolvedUrl("ExportPage.qml"), { fileUrl: url, fileName: name })
    }

    function startDownload() {
        var ext = map.visibleExtent()
        page.dlDone = 0
        page.dlTotal = 0
        var zmax = Math.min(17, map.zoom + 2)
        cache.downloadArea(ext.lat1, ext.lon1, ext.lat2, ext.lon2, map.zoom, zmax)
        showToast(i18n.tr("Préchargement zoom %1 à %2 lancé").arg(map.zoom).arg(zmax))
    }

    function showToast(msg) {
        toastLabel.text = msg
        toastRect.opacity = 1.0
        toastTimer.restart()
    }

    // Fond de page sombre (la carte recouvre tout, mais évite le flash
    // blanc au chargement / entre les tuiles)
    Rectangle {
        anchors.fill: parent
        color: Theme.bgPage
        z: -10
    }

    // Le beep toutes les 10 s est géré par le daemon GPS (utitibike-gpsd),
    // pas par l'app QML — il continue même quand l'app est gelée (SIGSTOP).

    // En second plan, plus aucun téléchargement de tuile :
    // l'appli ne dépense pratiquement rien en arrière-plan.
    TileCache { id: cache; paused: !appState.foreground }

    // brouter est une context property (définie dans main.cpp) pour que
    // HistoryPage puisse appeler loadGpxData() directement.
    // Le profil est bindé ici ; les signaux sont gérés via Connections.
    // Daemon GPS : resynchronise au démarrage (si un enregistrement était
    // en cours avant un redémarrage de l'app), importe le résultat final
    // dans l'historique après l'arrêt.
    Connections {
        target: daemonController
        function onDoneReceived() {
            var gpxContent = daemonController.lastGpxContent
            var distM = daemonController.distanceM
            var ascM = daemonController.ascentM
            // /!\ lastGpxContent est une QString C++ → chaîne JS en QML :
            // pas de méthode isEmpty() (TypeError) → tester length.
            if (gpxContent !== "" && gpxContent.length > 0) {
                var name = i18n.tr("Trajet %1 · %2 km")
                           .arg(new Date().toLocaleString(Qt.locale(), "dd/MM HH:mm"))
                           .arg((distM / 1000).toFixed(1))
                routeHistory.addEntry(name, distM, ascM, [], gpxContent, daemonController.startTime)
                showToast(i18n.tr("Parcours sauvegardé"))
            }
            // Arrêt automatique du daemon (batterie / durée max / disque)
            if (daemonController.stopReason === "battery") {
                showToast(i18n.tr("Arrêt auto : batterie à 20 %"))
                // L'enregistrement s'est arrêté pour batterie : on ferme
                // l'app (le daemon l'a peut-être déjà fermée par SIGTERM).
                QTimer.singleShot(1500, function() { Qt.quit() })
            }
            else if (daemonController.stopReason === "maxduration")
                showToast(i18n.tr("Arrêt auto : durée max atteinte (12 h)"))
            else if (daemonController.stopReason === "diskfull")
                showToast(i18n.tr("Arrêt auto : disque plein"))
            else if (daemonController.stopReason === "writeerror")
                showToast(i18n.tr("Arrêt auto : erreur d'écriture disque"))
            else if (daemonController.stopReason === "crash")
                showToast(i18n.tr("Arrêt après un crash — parcours sauvegardé"))
        }
        function onDaemonRestarted() {
            // Le daemon a crashé et a été relancé : le tracé reprend depuis
            // la dernière sauvegarde incrémentale.
            showToast(i18n.tr("Enregistrement repris après un crash du daemon"))
        }
    }
    Component.onCompleted: {
        // Vérifie si un enregistrement daemon était en cours (app redémarrée)
        daemonController.checkExistingRecording()
        // Récupère un éventuel en-cours.part laissé par un daemon tué brutalement
        if (daemonController.recoverOrphanPart()) {
            var gpxContent = daemonController.lastGpxContent
            if (gpxContent !== "" && gpxContent.length > 0) {
                var name = i18n.tr("Trajet %1 · %2 km")
                           .arg(new Date().toLocaleString(Qt.locale(), "dd/MM HH:mm"))
                           .arg((daemonController.distanceM / 1000).toFixed(1))
                routeHistory.addEntry(name, daemonController.distanceM,
                                      daemonController.ascentM, [], gpxContent,
                                      daemonController.startTime)
            }
            showToast(i18n.tr("Parcours récupéré après un arrêt brutal"))
            daemonController.notify(i18n.tr("Parcours récupéré après un arrêt brutal"))
        }
    }

    // Centre la carte sur le bounding box d'un tracé (itinéraire calculé
    // OU trajet chargé depuis l'historique) — sinon la carte reste sur la
    // vue restaurée et le tracé semble « ne correspondre à rien ».
    function fitToBounds(pts) {
        if (!pts || pts.length === 0)
            return
        var minLat = 90, maxLat = -90, minLon = 180, maxLon = -180
        for (var i = 0; i < pts.length; i++) {
            var p = pts[i]
            if (p.lat < minLat) minLat = p.lat
            if (p.lat > maxLat) maxLat = p.lat
            if (p.lon < minLon) minLon = p.lon
            if (p.lon > maxLon) maxLon = p.lon
        }
        map.centerLat = (minLat + maxLat) / 2
        map.centerLon = (minLon + maxLon) / 2
        // zoom adapté à l'étendue (~360°/2^z de côté)
        var d = Math.max(maxLat - minLat, maxLon - minLon)
        var z = 17
        while (z > 3 && d > 360 / Math.pow(2, z))
            z--
        map.zoom = z
    }

    Connections {
        target: brouter
        function onRouteReady(points, distanceM, ascentM) {
            page.routePoints = points
            page.routeDistance = distanceM
            page.routeAscent = ascentM
            page.routeGpx = brouter.lastGpx
            page.fitToBounds(points)
            // Toast avec difficulté + calories (visible immédiatement)
            var diff = page.difficultyLevel(distanceM, ascentM)
            var kcal = page.estimatedCalories(distanceM, ascentM)
            // Sauvegarde automatique dans l'historique (sauf si on
            // charge un itinéraire depuis l'historique — sinon doublon)
            if (!brouter.loadingFromHistory) {
                showToast(i18n.tr("Itinéraire calculé — %1 · ≈ %2 kcal").arg(diff).arg(kcal))
                routeHistory.addEntry("", distanceM, ascentM, page.waypoints, brouter.lastGpx)
            } else {
                brouter.loadingFromHistory = false
                showToast(i18n.tr("Itinéraire chargé — %1 · ≈ %2 kcal").arg(diff).arg(kcal))
            }
        }
        function onRouteFailed(message) {
            page.routePoints = []
            page.routeGpx = ""
            showToast(message)
        }
        // Alertes virage : vibration à 200m, 100m, 50m
        function onTurnAlert(severity) {
            if (severity === 0) haptics.vibrate(30)
            else if (severity === 1) haptics.vibrate(60)
            else haptics.vibrate(100)
        }
    }
    // Binding du profil BRouter (le context property n'a pas de binding QML)
    Binding {
        target: brouter
        property: "profile"
        value: page.profiles[page.profileIndex]
    }

    TileMapItem {
        id: map
        anchors.fill: parent
        cache: cache
        waypoints: page.waypoints
        route: page.routePoints
        currentPosition: page.liveHasPos ? { "lat": page.liveLat, "lon": page.liveLon } : ({})
        favoritePoints: favorites.entries

        // Vue persistée (view.json via viewState) : restaurée au démarrage,
        // puis chaque pan/zoom est remonté pour sauvegarde (debouncée 1 s).
        Component.onCompleted: {
            page.restoringView = true
            if (viewState.restored) {
                map.centerLat = viewState.centerLat
                map.centerLon = viewState.centerLon
                map.zoom = viewState.zoom
            }
            page.restoringView = false
        }
        onCenterLatChanged: {
            viewState.centerLat = map.centerLat
            if (!page.restoringView) page.userMovedMap = true
        }
        onCenterLonChanged: {
            viewState.centerLon = map.centerLon
            if (!page.restoringView) page.userMovedMap = true
        }
        onZoomChanged: {
            viewState.zoom = map.zoom
            if (!page.restoringView) page.userMovedMap = true
        }
    }

    // Nom du projet en permanence (haut gauche, sous le header)
    Label {
        z: 15
        anchors {
            top: parent.top
            left: parent.left
            margins: units.gu(1)
            topMargin: units.gu(8)
        }
        text: "Utitibike v" + appVersion
        fontSize: "x-small"
        font.bold: true
        // Texte clair + outline sombre : lisible sur tuiles claires ou sombres
        color: Theme.textPrimary
        style: Text.Outline
        styleColor: "#000000"
    }

    // Indicateur hors ligne (auto-détection réseau dans TileCache : 3 échecs
    // réseau consécutifs -> hors ligne, sonde toutes les 30 s pour le retour)
    SportCard {
        z: 15
        anchors {
            top: parent.top
            right: parent.right
            margins: units.gu(1)
            topMargin: units.gu(8)
        }
        visible: !cache.online
        padding: units.gu(0.5)
        radius: Theme.radiusChip
        border.color: Theme.accentRed
        width: offlineLabel.width + units.gu(2)
        height: offlineLabel.height + units.gu(1)
        Label {
            id: offlineLabel
            anchors.centerIn: parent
            text: i18n.tr("Hors ligne")
            color: Theme.textOnAccent
            fontSize: "x-small"
            font.bold: true
        }
    }

    // Bandeau de navigation turn-by-turn (visible en navigation) :
    // pilule bleue centrée (max 85 % de large), style « instruction de nav »
    SportCard {
        id: navBanner
        z: 10
        anchors {
            top: parent.top
            topMargin: units.gu(1)
            horizontalCenter: parent.horizontalCenter
        }
        width: Math.min(parent.width - units.gu(4), parent.width * 0.85)
        height: navCol.height + units.gu(1.5)
        padding: units.gu(0.75)
        color: Theme.accentBlue
        border.color: "transparent"
        visible: brouter.navigation
        transform: Translate {
            y: brouter.navigation ? 0 : -navBanner.height - units.gu(1)
            Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
        }

        Column {
            id: navCol
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            spacing: units.gu(0.3)
            Label {
                width: parent.width
                color: Theme.textOnAccent
                fontSize: "medium"
                font.bold: true
                wrapMode: Text.Wrap
                text: brouter.nextInstruction || "…"
            }
            Label {
                color: "#ccffffff"
                fontSize: "small"
                text: brouter.nextInstructionDistance > 0
                      ? i18n.tr("dans %1 m").arg(Math.round(brouter.nextInstructionDistance))
                      : ""
            }
        }
    }

    // Profil d'altitude en overlay en haut de l'écran (visible quand un
    // itinéraire est calculé). Se place sous le bandeau de navigation.
    SportCard {
        id: elevationOverlay
        z: 5
        anchors {
            left: parent.left
            right: parent.right
            leftMargin: units.gu(1)
            rightMargin: units.gu(1)
        }
        y: (navBanner.visible ? navBanner.height + units.gu(2) : units.gu(1))
           + (routeInfoBanner.visible ? routeInfoBanner.height + units.gu(0.5) : 0)
        height: units.gu(6)
        padding: units.gu(0.5)
        visible: page.routePoints.length > 1
        Behavior on y { NumberAnimation { duration: 250 } }

        ElevationProfile {
            anchors.fill: parent
            points: page.routePoints
        }
    }

    // Tableau de bord (overlay bas-gauche, visible si GPS actif ou enregistrement)
    // Gros chiffres vitesse, unité km/h en cyan électrique
    SportCard {
        id: dashboard
        z: 10
        x: units.gu(1)
        y: parent.height - height - (page.panelOpen ? panel.height + units.gu(2) : units.gu(2))
        width: dashCol.width + units.gu(2)
        height: dashCol.height + units.gu(1)
        padding: units.gu(1)
        visible: gpsTracker.active || daemonController.recording
        Behavior on y { NumberAnimation { duration: 200 } }

        Column {
            id: dashCol
            anchors.centerIn: parent
            spacing: units.gu(0.2)
            Row {
                spacing: units.gu(1.5)
                Label {
                    color: Theme.textPrimary
                    fontSize: "x-large"
                    font.bold: true
                    text: page.liveHasFix ? page.liveSpeedKmh.toFixed(1) : "--"
                }
                Label {
                    color: Theme.accentCyan
                    fontSize: "small"
                    text: "km/h"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Label {
                color: Theme.textPrimary
                fontSize: "small"
                visible: daemonController.recording
                text: (daemonController.distanceM / 1000).toFixed(2) + " km"
                    + "  ·  D+ " + Math.round(daemonController.ascentM) + " m"
                    + "  ·  " + formatDuration(daemonController.elapsedSec)
            }
            Label {
                color: Theme.accentAmber
                fontSize: "x-small"
                visible: daemonController.recording && daemonController.paused
                text: i18n.tr("Pause auto (arrêt détecté)")
            }
            Label {
                color: Theme.accentRed
                fontSize: "x-small"
                visible: daemonController.recording && daemonController.writeError
                text: i18n.tr("Erreur d'écriture disque")
            }
            // Voyant d'état du signal GPS : vert fixe = vrai fix satellite ;
            // orange clignotant = position approximative (dernier lieu connu
            // ou réseau) ; rouge clignotant = recherche, aucune position.
            Row {
                spacing: units.gu(1)
                Rectangle {
                    id: fixDot
                    width: units.gu(1.6)
                    height: width
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: page.liveHasFix ? Theme.accentGreen
                                          : (page.liveHasApprox ? Theme.accentAmber
                                                                : Theme.accentRed)
                    SequentialAnimation on opacity {
                        running: !page.liveHasFix
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
                Label {
                    color: Theme.textSecondary
                    fontSize: "x-small"
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.liveRecording
                          ? (daemonController.hasFix ? i18n.tr("GPS actif")
                             : (daemonController.approx ? i18n.tr("Position approximative…")
                                                        : i18n.tr("Recherche du signal GPS…")))
                          : gpsTracker.statusText
                }
            }
        }
    }

    // Bandeau « départ manuel » : affiché quand le daemon enregistre depuis
    // plus de 6 min sans aucun fix satellite — un appui simple sur la carte
    // définit le point de départ.
    SportCard {
        id: manualStartBanner
        z: 20
        anchors {
            left: parent.left
            right: parent.right
            margins: units.gu(1)
        }
        y: units.gu(1)
        height: units.gu(5)
        padding: units.gu(1)
        visible: daemonController.recording && daemonController.awaitManualStart
        Behavior on y { NumberAnimation { duration: 200 } }
        Row {
            anchors.centerIn: parent
            spacing: units.gu(1)
            Rectangle {
                width: units.gu(1.6)
                height: width
                radius: width / 2
                color: Theme.accentRed
                anchors.verticalCenter: parent.verticalCenter
                SequentialAnimation on opacity {
                    running: manualStartBanner.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
            Label {
                width: parent.width - units.gu(3.6)
                wrapMode: Text.WordWrap
                color: Theme.textPrimary
                fontSize: "small"
                anchors.verticalCenter: parent.verticalCenter
                text: i18n.tr("Pas de signal GPS — touchez la carte pour définir le départ")
            }
        }
    }

    function formatDuration(secs) {
        var h = Math.floor(secs / 3600)
        var m = Math.floor((secs % 3600) / 60)
        var s = secs % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") + m : m + ":" + (s < 10 ? "0" : "") + s)
    }

    // Barre de recherche Nominatim
    SportCard {
        id: searchBar
        z: 20
        anchors {
            left: parent.left
            right: parent.right
            margins: units.gu(1)
        }
        y: navBanner.visible ? navBanner.height + units.gu(1.5) : units.gu(1)
        height: searchCol.height + units.gu(1)
        padding: units.gu(0.5)
        visible: page.searchOpen
        Behavior on y { NumberAnimation { duration: 250 } }

        Column {
            id: searchCol
            width: parent.width - units.gu(2)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: units.gu(0.5)

            Row {
                width: parent.width
                spacing: units.gu(0.5)
                TextField {
                    id: searchField
                    width: parent.width - units.gu(6)
                    height: units.gu(5)
                    placeholderText: i18n.tr("Rechercher un lieu…")
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onAccepted: page.doSearch(text)
                    Keys.onReturnPressed: page.doSearch(text)
                    Keys.onEnterPressed: page.doSearch(text)
                }
                Button {
                    width: units.gu(5)
                    height: units.gu(5)
                    text: "→"
                    color: Theme.accentBlue
                    enabled: searchField.text.length > 2 && !page.searchBusy
                    onClicked: page.doSearch(searchField.text)
                }
            }

            ListView {
                width: parent.width
                height: Math.min(units.gu(20), searchResults.length * units.gu(5))
                visible: page.searchResults.length > 0
                model: page.searchResults
                spacing: units.gu(0.2)
                clip: true
                delegate: Rectangle {
                    width: parent.width
                    height: units.gu(4.5)
                    color: index % 2 === 0 ? Theme.bgSection : Theme.bgInput
                    Label {
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            margins: units.gu(0.5)
                        }
                        fontSize: "small"
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        text: modelData.display_name
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            map.centerLat = modelData.lat
                            map.centerLon = modelData.lon
                            map.zoom = Math.max(map.zoom, 15)
                            page.searchOpen = false
                            page.searchResults = []
                            searchField.text = ""
                        }
                    }
                }
            }
        }
    }

    // Recherche via SearchClient (C++/QNetworkAccessManager) — le
    // XMLHttpRequest QML ne pose pas le User-Agent correctement sur
    // Ubuntu Touch, Nominatim renvoie HTTP 403.
    function doSearch(query) {
        if (query.length < 3) return
        page.searchBusy = true
        page.searchResults = []
        searchClient.search(query)
    }

    Connections {
        target: searchClient
        function onSearchFinished(results) {
            page.searchBusy = false
            page.searchResults = results
            if (results.length === 0)
                showToast(i18n.tr("Aucun résultat"))
        }
        function onSearchFailed(message) {
            page.searchBusy = false
            showToast(message)
        }
    }

    PinchArea {
        id: pinch
        anchors.fill: parent
        property real prevScale: 1.0
        onPinchStarted: { prevScale = pinch.scale }
        onPinchUpdated: {
            if (pinch.scale / prevScale > 1.35) {
                map.zoomAbout(1, pinch.center.x, pinch.center.y)
                prevScale = pinch.scale
            } else if (prevScale / pinch.scale > 1.35) {
                map.zoomAbout(-1, pinch.center.x, pinch.center.y)
                prevScale = pinch.scale
            }
        }

        MouseArea {
            id: mapMouse
            anchors.fill: parent
            property real lastX: 0
            property real lastY: 0
            property real pressX: 0
            property real pressY: 0
            property bool longPressDone: false
            // Centre/zoom de la carte au moment du press : si la carte bouge
            // entre-temps (GPS follow actif), le appui long est annulé pour
            // éviter un waypoint à des coordonnées fausses (point fantôme).
            property real pressCenterLat: 0
            property real pressCenterLon: 0
            property int pressZoom: 0

            // Manual long-press detection: onPressAndHold is erratic on touch.
            // A first buzz at 500 ms warns that the press is being held; the
            // waypoint is added at 900 ms (a longer, deliberate press) with a
            // confirmation buzz. Moving the finger cancels both timers.
            Timer {
                id: warnTimer
                interval: 500
                onTriggered: haptics.vibrate(40)
            }
            Timer {
                id: addTimer
                interval: 900
                onTriggered: {
                    // Sécurité : ne rien faire si le MouseArea n'est plus
                    // pressé (événement passé à travers un overlay, etc.)
                    if (!mapMouse.pressed)
                        return
                    // Annuler si la carte a bougé depuis le press (GPS follow,
                    // centrage auto, etc.) : les coordonnées écran ne
                    // correspondraient plus à la bonne position.
                    if (map.centerLat !== mapMouse.pressCenterLat
                            || map.centerLon !== mapMouse.pressCenterLon
                            || map.zoom !== mapMouse.pressZoom) {
                        return
                    }
                    mapMouse.longPressDone = true
                    var ll = map.itemToLatLon(mapMouse.pressX, mapMouse.pressY)
                    page.addWaypoint(ll.lat, ll.lon)
                    haptics.vibrate(80)
                }
            }

            onPressed: {
                pressX = mouse.x
                pressY = mouse.y
                lastX = mouse.x
                lastY = mouse.y
                longPressDone = false
                pressCenterLat = map.centerLat
                pressCenterLon = map.centerLon
                pressZoom = map.zoom
                warnTimer.restart()
                addTimer.restart()
            }
            onPositionChanged: {
                if (Math.abs(mouse.x - pressX) > units.gu(1)
                        || Math.abs(mouse.y - pressY) > units.gu(1)) {
                    warnTimer.stop()
                    addTimer.stop()
                }
                if (!longPressDone) {
                    map.panBy(mouse.x - lastX, mouse.y - lastY)
                    lastX = mouse.x
                    lastY = mouse.y
                }
            }
            onReleased: {
                warnTimer.stop()
                addTimer.stop()
                // Départ manuel : si le daemon attend un point de départ (pas de
                // fix GPS), un appui simple sur la carte définit le départ.
                if (daemonController.recording && daemonController.awaitManualStart
                        && !longPressDone) {
                    var ll = map.itemToLatLon(mapMouse.pressX, mapMouse.pressY)
                    daemonController.setManualStart(ll.lat, ll.lon)
                    showToast(i18n.tr("Départ défini sur la carte"))
                }
            }
            onCanceled: {
                warnTimer.stop()
                addTimer.stop()
            }
        }
    }

    // Bandeau permanent difficulté + calories (toujours visible quand
    // un itinéraire est calculé, se met à jour automatiquement à chaque
    // nouveau parcours via les bindings QML sur routeDistance/routeAscent)
    // Chip flottant « badge sportif » : pastille difficulté + kcal + km + D+
    SportCard {
        id: routeInfoBanner
        z: 18
        visible: page.routeDistance > 0
        anchors {
            left: parent.left
            right: parent.right
            margins: units.gu(1)
        }
        y: page.searchOpen ? searchBar.y + searchBar.height + units.gu(0.5) : units.gu(1)
        height: units.gu(4)
        padding: units.gu(0.75)
        radius: height / 2
        Behavior on y { NumberAnimation { duration: 250 } }

        Row {
            anchors.centerIn: parent
            spacing: units.gu(1.2)

            Rectangle {
                width: units.gu(1.2)
                height: units.gu(1.2)
                radius: width / 2
                color: page.difficultyColor(page.routeDistance, page.routeAscent)
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 300 } }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: page.difficultyLevel(page.routeDistance, page.routeAscent)
                color: Theme.textPrimary
                fontSize: "small"
                font.bold: true
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: Theme.textSecondary
                fontSize: "small"
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "≈ " + page.estimatedCalories(page.routeDistance, page.routeAscent) + " kcal"
                color: Theme.textPrimary
                fontSize: "small"
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: Theme.textSecondary
                fontSize: "small"
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: (page.routeDistance / 1000).toFixed(1) + " km"
                color: Theme.textPrimary
                fontSize: "small"
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                color: Theme.textSecondary
                fontSize: "small"
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "+" + Math.round(page.routeAscent) + " m"
                color: Theme.textPrimary
                fontSize: "small"
            }
        }
    }

    Column {
        id: zoomCol
        anchors {
            right: parent.right
            rightMargin: units.gu(1)
            verticalCenter: parent.verticalCenter
        }
        spacing: units.gu(1)
        // Bouton couche de carte : cycle OSM → Cyclable → Sombre → Satellite
        // (accès direct, sans ouvrir le panneau). Nom court = pas dépendant
        // des icônes du thème.
        SportCard {
            id: tileLayerCard
            width: units.gu(6)
            height: units.gu(6)
            radius: width / 2
            padding: 0
            property var keys: ["osm", "cyclosm", "dark", "satellite"]
            property var shortNames: ({ "osm": "OSM", "cyclosm": "Cyc", "dark": "Nuit", "satellite": "Sat" })
            Label {
                anchors.centerIn: parent
                // SportCard reparent les enfants dans inner : référencer par id
                text: tileLayerCard.shortNames[cache.tileServer] || "OSM"
                color: Theme.accentOrange
                fontSize: "x-small"
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    var i = tileLayerCard.keys.indexOf(cache.tileServer)
                    cache.tileServer = tileLayerCard.keys[(i + 1) % tileLayerCard.keys.length]
                    haptics.vibrate(20)
                }
            }
        }
        SportCard {
            width: units.gu(6)
            height: units.gu(6)
            radius: width / 2
            padding: 0
            Icon {
                anchors.centerIn: parent
                name: "zoom-in"
                width: units.gu(3)
                height: width
                color: Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: map.zoomAbout(1, map.width / 2, map.height / 2)
            }
        }
        SportCard {
            width: units.gu(6)
            height: units.gu(6)
            radius: width / 2
            padding: 0
            Icon {
                anchors.centerIn: parent
                name: "zoom-out"
                width: units.gu(3)
                height: width
                color: Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: map.zoomAbout(-1, map.width / 2, map.height / 2)
            }
        }
    }

    ActivityIndicator {
        anchors.centerIn: parent
        running: brouter.busy
        visible: brouter.busy
    }

    SportCard {
        z: 15
        anchors {
            top: parent.top
            topMargin: units.gu(8)
            horizontalCenter: parent.horizontalCenter
        }
        width: hintLabel.width + units.gu(2)
        height: hintLabel.height + units.gu(1)
        padding: units.gu(0.5)
        radius: height / 2
        visible: page.waypoints.length === 0
        Label {
            id: hintLabel
            anchors.centerIn: parent
            text: i18n.tr("Appui long sur la carte : ajouter un point")
            color: Theme.textSecondary
            fontSize: "small"
        }
    }

    SportCard {
        id: toastRect
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
        }
        anchors.bottomMargin: page.panelOpen ? panel.height + units.gu(2) : units.gu(2)
        width: Math.min(parent.width - units.gu(4), toastLabel.width + units.gu(2))
        height: toastLabel.height + units.gu(1)
        padding: units.gu(0.5)
        radius: Theme.radiusChip
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: Theme.textPrimary
            fontSize: "small"
            wrapMode: Text.Wrap
            width: Math.min(page.width - units.gu(10), toastLabel.implicitWidth)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Timer {
        id: toastTimer
        interval: 2800
        onTriggered: toastRect.opacity = 0
    }

    Rectangle {
        id: panel
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: Math.min(units.gu(44), panelCol.height + units.gu(2))
        color: Theme.bgPage
        radius: Theme.radiusCard
        transform: Translate {
            id: panelShift
            y: page.panelOpen ? 0 : panel.height + units.gu(1)
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        }

        // Bloque les événements souris qui passent à travers le Flickable
        // (marge de 1 gu, ou contenu non scrollable). Sans ça, un appui long
        // sur le panneau atteint la carte en dessous → waypoint fantôme.
        // Le Flickable est déclaré après → au-dessus → reçoit les événements
        // en premier. Ce MouseArea n'intercepte que les événements résiduels.
        MouseArea {
            anchors.fill: parent
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: units.gu(1)
            contentHeight: panelCol.height + units.gu(2)
            clip: true

            Column {
                id: panelCol
                width: parent.width
                spacing: units.gu(1)

                Label {
                    text: i18n.tr("Itinéraire — %1 point(s)").arg(page.waypoints.length)
                    fontSize: "medium"
                    font.bold: true
                    color: Theme.textPrimary
                }

                SportCard {
                    width: parent.width
                    height: Math.min(units.gu(16), units.gu(3.4) * page.waypoints.length + units.gu(1))
                    padding: units.gu(0.5)
                    visible: page.waypoints.length > 0
                    ListView {
                        id: wpList
                        anchors.fill: parent
                        clip: true
                        spacing: units.gu(0.2)
                        model: page.waypoints
                        delegate: Row {
                            width: wpList.width
                            height: units.gu(3)
                            spacing: units.gu(0.5)
                            Label {
                                width: parent.width - units.gu(6.5)
                                anchors.verticalCenter: parent.verticalCenter
                                fontSize: "small"
                                color: Theme.textPrimary
                                text: (index === 0 ? i18n.tr("Départ") : (index === page.waypoints.length - 1 ? i18n.tr("Arrivée") : i18n.tr("Étape %1").arg(index)))
                                      + "  ·  %1, %2".arg(modelData.lat.toFixed(5)).arg(modelData.lon.toFixed(5))
                            }
                            Button {
                                width: units.gu(5)
                                height: units.gu(5)
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.accentRed
                                onClicked: page.removeWaypoint(index)
                                Icon {
                                    anchors.centerIn: parent
                                    name: "delete"
                                    width: units.gu(2.5)
                                    height: units.gu(2.5)
                                    color: Theme.textOnAccent
                                }
                            }
                        }
                    }
                }

                // CTA principal : calculer l'itinéraire dès que deux points
                // sont posés. Pleine largeur, dégradé orange→ambre, plus grand.
                Rectangle {
                    width: parent.width
                    height: units.gu(6)
                    radius: Theme.radiusButton
                    opacity: page.waypoints.length >= 2 && !brouter.busy ? 1.0 : 0.45
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.accentOrange }
                        GradientStop { position: 1.0; color: Theme.ubuntuAubergine }
                    }
                    Label {
                        anchors.centerIn: parent
                        text: brouter.busy ? i18n.tr("Calcul…") : i18n.tr("Calculer l'itinéraire")
                        color: Theme.textOnAccent
                        fontSize: "medium"
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: page.waypoints.length >= 2 && !brouter.busy
                        onClicked: brouter.requestRoute(page.waypoints)
                    }
                }

                Row {
                    width: parent.width
                    Label {
                        text: i18n.tr("Profil BRouter")
                        fontSize: "small"
                        font.bold: true
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    // Badge difficulté estimé (visible si itinéraire calculé)
                    Rectangle {
                        visible: page.routeDistance > 0
                        color: page.difficultyColor(page.routeDistance, page.routeAscent)
                        radius: units.gu(0.5)
                        height: units.gu(2.5)
                        width: badgeLabel.width + units.gu(1)
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            id: badgeLabel
                            anchors.centerIn: parent
                            text: page.difficultyLevel(page.routeDistance, page.routeAscent)
                            fontSize: "x-small"
                            font.bold: true
                            color: Theme.textOnAccent
                        }
                    }
                }
                SegmentedControl {
                    width: parent.width
                    model: [
                        { label: i18n.tr("Rando"), value: "trekking" },
                        { label: i18n.tr("Rapide"), value: "fastbike" },
                        { label: i18n.tr("Sécurisé"), value: "safety" },
                        { label: i18n.tr("Court"), value: "shortest" }
                    ]
                    currentIndex: page.profileIndex
                    onSelected: page.profileIndex = index
                }

                Label {
                    text: i18n.tr("Dénivelé")
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }
                SegmentedControl {
                    width: parent.width
                    activeColor: Theme.accentAmber
                    model: [
                        { label: i18n.tr("Facile"), value: 500 },
                        { label: i18n.tr("Normal"), value: 50 },
                        { label: i18n.tr("Difficile"), value: -100 }
                    ]
                    currentIndex: brouter.uphillCost >= 300 ? 0 : (brouter.uphillCost < 0 ? 2 : 1)
                    onSelected: brouter.uphillCost = value
                }
                // Slider difficulté : gauche = Facile (évite les côtes, coût élevé),
                // droite = Sportif (cherche les côtes, coût négatif).
                // minimumValue > maximumValue est volontaire : le Slider QML
                // positionne la poignée à (value - min) / (max - min), donc
                // value=500 (facile) → position 0 (gauche), value=-200 (sportif)
                // → position 1 (droite). Cohérent avec l'ordre des boutons
                // Facile | Normal | Difficile au-dessus.
                Slider {
                    id: difficultySlider
                    width: parent.width
                    minimumValue: 500
                    maximumValue: -200
                    stepSize: 10
                    Component.onCompleted: value = brouter.uphillCost
                    onValueChanged: brouter.uphillCost = Math.round(value)
                }
                // Le Slider Ubuntu.Components ne rafraîchit pas sa poignée
                // quand la propriété C++ change via un binding — on pousse
                // la valeur explicitement.
                Connections {
                    target: brouter
                    function onUphillCostChanged() {
                        difficultySlider.value = brouter.uphillCost
                    }
                }
                Label {
                    fontSize: "small"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.textPrimary
                    text: brouter.uphillCost < 0
                          ? i18n.tr("Sportif : cherche les côtes (bonus D+ %1)").arg(-brouter.uphillCost)
                          : brouter.uphillCost === 0
                            ? i18n.tr("Ignore le dénivelé dans le calcul")
                            : brouter.uphillCost < 100
                              ? i18n.tr("Normal : pénalise légèrement les montées")
                              : i18n.tr("Facile : évite les côtes (coût %1)").arg(brouter.uphillCost)
                }

                // Poids pour l'estimation des calories
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Label {
                        text: i18n.tr("Poids")
                        fontSize: "small"
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        width: units.gu(8)
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: page.userWeightKg
                        onTextChanged: {
                            var w = parseFloat(text)
                            if (!isNaN(w) && w >= 30 && w <= 200)
                                page.userWeightKg = w
                        }
                    }
                    Label {
                        text: "kg"
                        fontSize: "small"
                        color: Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Label {
                    text: i18n.tr("Apparence")
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }
                SegmentedControl {
                    width: parent.width
                    property var modeKeys: ["auto", "dark", "light"]
                    model: [
                        { label: i18n.tr("Auto"), value: "auto" },
                        { label: i18n.tr("Sombre"), value: "dark" },
                        { label: i18n.tr("Clair"), value: "light" }
                    ]
                    currentIndex: Math.max(0, modeKeys.indexOf(viewState.themeMode))
                    onSelected: viewState.themeMode = value
                }
                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Label {
                    text: i18n.tr("GPS & Navigation")
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Label {
                        text: i18n.tr("Bip de confirmation")
                        color: Theme.textSecondary
                        fontSize: "small"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: daemonController.beepEnabled
                        onCheckedChanged: daemonController.beepEnabled = checked
                    }
                }
                Label {
                    text: i18n.tr("Source GPS")
                    fontSize: "x-small"
                    color: Theme.textSecondary
                }
                SegmentedControl {
                    width: parent.width
                    property var modeKeys: ["auto", "satellite", "network"]
                    model: [
                        { label: i18n.tr("Auto"), value: "auto" },
                        { label: i18n.tr("Satellite"), value: "satellite" },
                        { label: i18n.tr("Réseau"), value: "network" }
                    ]
                    currentIndex: Math.max(0, modeKeys.indexOf(viewState.positioningMode))
                    onSelected: {
                        viewState.positioningMode = value
                        gpsTracker.setPositioningMode(value)
                        daemonController.setPositioningMode(value)
                    }
                }
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        height: units.gu(6)
                        text: gpsTracker.active ? i18n.tr("GPS actif") : i18n.tr("Activer GPS")
                        iconName: "location"
                        color: gpsTracker.active ? Theme.accentGreen : Theme.bgSection
                        onClicked: {
                            if (gpsTracker.active) gpsTracker.stop()
                            // Pendant un enregistrement, le daemon gère déjà le
                            // GPS : ne pas ouvrir une 2e source (batterie).
                            else if (daemonController.recording) showToast(i18n.tr("GPS déjà géré par l'enregistrement en cours"))
                            else gpsTracker.start()
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        height: units.gu(6)
                        text: page.followGps ? i18n.tr("Suivi GPS") : i18n.tr("Centrer GPS")
                        iconName: "edit-find"
                        enabled: page.liveHasPos
                        color: page.followGps ? Theme.accentBlue : Theme.bgSection
                        onClicked: {
                            page.followGps = !page.followGps
                            if (page.followGps && page.liveHasPos) {
                                map.centerLat = page.liveLat
                                map.centerLon = page.liveLon
                            }
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        height: units.gu(6)
                        text: brouter.navigation ? i18n.tr("Arrêter nav.") : i18n.tr("Naviguer")
                        iconName: "go-next"
                        enabled: page.routePoints.length > 1
                        color: brouter.navigation ? Theme.accentRed : Theme.accentBlue
                        onClicked: {
                            if (brouter.navigation) {
                                brouter.stopNavigation()
                                // Ne relâche l'écran que si l'enregistrement
                                // GPS daemon n'est pas aussi en cours.
                                if (!daemonController.recording)
                                    displayKeeper.release()
                            } else {
                                brouter.startNavigation()
                                gpsTracker.start()
                                page.followGps = true
                                displayKeeper.hold()
                                if (gpsTracker.hasFix) {
                                    map.centerLat = gpsTracker.latitude
                                    map.centerLon = gpsTracker.longitude
                                }
                            }
                        }
                    }
                }

                // Sémantique : « Enregistrer » démarre le daemon GPS détaché
                // (utitibike-gpsd), « Arrêter » le stoppe et sauvegarde le
                // parcours. Le daemon tourne sans UI et sans keep-display-on :
                // économie batterie maximale. Il meurt automatiquement si
                // l'app crash (PR_SET_PDEATHSIG + vérification PID).
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Button {
                        width: (parent.width - units.gu(0.5)) / 2
                        height: units.gu(6)
                        text: daemonController.recording ? i18n.tr("Arrêter") : i18n.tr("Enregistrer")
                        iconName: daemonController.recording ? "media-playback-stop" : "media-record"
                        color: daemonController.recording ? Theme.accentRed : Theme.accentGreen
                        onClicked: {
                            if (daemonController.recording) {
                                daemonController.stopRecording()
                                showToast(i18n.tr("Enregistrement arrêté"))
                            } else {
                                daemonController.startRecording()
                                // Une seule source GPS active (le daemon) et
                                // écran non retenu allumé : économie batterie.
                                if (gpsTracker.active)
                                    gpsTracker.stop()
                                displayKeeper.release()
                            }
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(0.5)) / 2
                        height: units.gu(6)
                        text: i18n.tr("Effacer tracé")
                        iconName: "edit-clear"
                        color: Theme.bgSection
                        enabled: daemonController.pointCount > 0
                        onClicked: {
                            if (daemonController.recording)
                                daemonController.stopRecording()
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Exporter GPX")
                    iconName: "share"
                    color: Theme.bgSection
                    enabled: page.routeGpx !== ""
                    onClicked: page.exportNow()
                }

                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Partager le GPX")
                    color: Theme.bgSection
                    enabled: page.routeGpx !== ""
                    onClicked: {
                        var url = brouter.exportGpx(page.routeGpx)
                        if (url === "") {
                            showToast(i18n.tr("Échec d'écriture du GPX"))
                            return
                        }
                        var name = url.substring(url.lastIndexOf("/") + 1)
                        pageStack.push(Qt.resolvedUrl("ExportPage.qml"),
                                       { fileUrl: url, fileName: name, handlerName: "Share" })
                    }
                }

                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Charger un GPX")
                    iconName: "document-open"
                    color: Theme.bgSection
                    onClicked: {
                        gpxPicker.visible = true
                    }
                }

                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Historique (%1)").arg(routeHistory.entries.length)
                    iconName: "view-list-symbolic"
                    color: Theme.bgSection
                    onClicked: pageStack.push(Qt.resolvedUrl("HistoryPage.qml"))
                }

                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Aide")
                    iconName: "info"
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

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Label {
                    text: i18n.tr("Favoris (%1)").arg(favorites.entries.length)
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }
                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Ajouter ma position aux favoris")
                    iconName: "starred"
                    color: Theme.bgSection
                    enabled: page.liveHasPos
                    onClicked: {
                        var name = i18n.tr("Position %1").arg(new Date().toLocaleTimeString())
                        favorites.add(name, page.liveLat, page.liveLon, "position")
                        showToast(i18n.tr("Favori ajouté"))
                    }
                }
                Repeater {
                    model: favorites.entries
                    SportCard {
                        width: parent.width
                        height: favRow.height + units.gu(1)
                        padding: units.gu(0.5)
                        Row {
                            id: favRow
                            width: parent.width - units.gu(1)
                            anchors.centerIn: parent
                            spacing: units.gu(0.5)
                            Label {
                                width: parent.width - units.gu(10)
                                fontSize: "small"
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                text: modelData.name + "  ·  " + modelData.category
                            }
                            Button {
                                width: units.gu(5)
                                height: units.gu(5)
                                text: "→"
                                color: Theme.accentBlue
                                onClicked: {
                                    map.centerLat = modelData.lat
                                    map.centerLon = modelData.lon
                                    map.zoom = Math.max(map.zoom, 15)
                                }
                            }
                            Button {
                                width: units.gu(5)
                                height: units.gu(5)
                                color: Theme.accentRed
                                onClicked: favorites.remove(index)
                                Icon {
                                    anchors.centerIn: parent
                                    name: "delete"
                                    width: units.gu(2)
                                    height: units.gu(2)
                                    color: Theme.textOnAccent
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Label {
                    text: i18n.tr("Garmin Connect")
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    fontSize: "small"
                    color: Theme.textSecondary
                    text: i18n.tr("Le GPX exporté contient des horodatages : Garmin Connect l'accepte (menu « Importer des données » du site).")
                }
                Button {
                    width: parent.width
                    height: units.gu(6)
                    text: i18n.tr("Ouvrir Garmin Connect dans le navigateur")
                    color: Theme.bgSection
                    onClicked: {
                        if (!urlOpener.openUrl("https://connect.garmin.com/"))
                            showToast(i18n.tr("Impossible d'ouvrir le navigateur"))
                    }
                }

                Label {
                    visible: page.routeDistance > 0
                    fontSize: "medium"
                    font.bold: true
                    color: Theme.textPrimary
                    text: "%1 km  ·  D+ %2 m".arg((page.routeDistance / 1000).toFixed(1)).arg(Math.round(page.routeAscent))
                }

                // Difficulté estimée + calories (apparaît avec l'itinéraire)
                Label {
                    visible: page.routeDistance > 0
                    fontSize: "small"
                    font.bold: true
                    color: page.difficultyColor(page.routeDistance, page.routeAscent)
                    text: i18n.tr("Difficulté : %1").arg(page.difficultyLevel(page.routeDistance, page.routeAscent))
                }
                Label {
                    visible: page.routeDistance > 0
                    fontSize: "small"
                    color: Theme.textSecondary
                    text: i18n.tr("≈ %1 kcal (estimation pour %2 kg)")
                          .arg(page.estimatedCalories(page.routeDistance, page.routeAscent))
                          .arg(page.userWeightKg)
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: Theme.bgCardBorder
                }

                Label {
                    text: i18n.tr("Carte hors ligne (tuiles OpenStreetMap)")
                    fontSize: "small"
                    font.bold: true
                    color: Theme.textPrimary
                }
                SegmentedControl {
                    width: parent.width
                    property var tileKeys: ["osm", "cyclosm", "dark", "satellite"]
                    model: [
                        { label: i18n.tr("Standard"), value: "osm" },
                        { label: i18n.tr("Cyclable"), value: "cyclosm" },
                        { label: i18n.tr("Sombre"), value: "dark" },
                        { label: i18n.tr("Satellite"), value: "satellite" }
                    ]
                    currentIndex: Math.max(0, tileKeys.indexOf(cache.tileServer))
                    onSelected: cache.tileServer = value
                }
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        height: units.gu(6)
                        text: i18n.tr("Précharger la zone affichée")
                        iconName: "view-refresh"
                        color: Theme.bgSection
                        onClicked: page.startDownload()
                    }
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        height: units.gu(6)
                        text: i18n.tr("Annuler")
                        iconName: "edit-undo"
                        color: Theme.bgSection
                        enabled: page.dlTotal > 0 && page.dlDone < page.dlTotal
                        onClicked: {
                            cache.cancelAll()
                            page.dlTotal = 0
                            page.dlDone = 0
                        }
                    }
                }
                ProgressBar {
                    width: parent.width
                    visible: page.dlTotal > 0
                    minimumValue: 0
                    maximumValue: page.dlTotal
                    value: page.dlDone
                }
                Label {
                    visible: page.dlTotal > 0
                    fontSize: "small"
                    color: Theme.textSecondary
                    text: i18n.tr("Tuiles : %1 / %2").arg(page.dlDone).arg(page.dlTotal)
                }
            }
        }
    }

    // FAB : cercle dégradé orange→ambre avec ombre simulée (pas de
    // DropShadow fiable sur UT arm64 — rectangle décalé derrière)
    Item {
        id: panelToggle
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: units.gu(2)
            // Panneau ouvert : le FAB remonte au-dessus du panneau (sinon il
            // est recouvert/rogne en bas à droite).
            bottomMargin: page.panelOpen ? panel.height + units.gu(2) : units.gu(2)
        }
        width: Theme.fabSize
        height: Theme.fabSize

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
            Icon {
                anchors.centerIn: parent
                name: page.panelOpen ? "close" : "navigation-menu"
                width: Theme.fabIconSize
                height: Theme.fabIconSize
                color: Theme.textOnAccent
            }
            MouseArea {
                anchors.fill: parent
                onClicked: page.panelOpen = !page.panelOpen
            }
        }
    }

    header: AppHeader {
        title: i18n.tr("Utitibike")
        trailingActionBar.actions: [
            Action {
                text: i18n.tr("Rechercher")
                iconName: "search"
                onTriggered: page.searchOpen = !page.searchOpen
            },
            Action {
                text: i18n.tr("Itinéraire")
                iconName: "navigation-menu"
                onTriggered: page.panelOpen = !page.panelOpen
            }
        ]
    }

    Connections {
        target: cache
        function onProgress(done, total) {
            page.dlDone = done
            page.dlTotal = total
        }
    }

    // Préchargement des tuiles autour de la position pendant l'enregistrement
    // (toutes les 5 min, ±~900 m, zooms 15-17) : la carte reste utilisable
    // hors-ligne ensuite. cache.paused vaut vrai quand l'app est en
    // arrière-plan : aucun téléchargement tant que l'app est gelée.
    Timer {
        id: preloadTimer
        interval: 300000
        repeat: true
        running: daemonController.recording && daemonController.hasFix
        onTriggered: {
            var d = 0.008
            cache.downloadArea(daemonController.latitude - d, daemonController.longitude - d,
                               daemonController.latitude + d, daemonController.longitude + d,
                               15, 17)
        }
    }

    // Quand l'app revient au premier plan, force le redessin de la carte :
    // le QQuickPaintedItem ne se repaint pas tout seul après une mise en
    // arrière-plan (la surface Mir est suspendue), l'écran reste blanc.
    Connections {
        target: appState
        function onForegroundChanged() {
            if (appState.foreground)
                map.update()
        }
    }

    // Centrage auto sur le GPS + mise à jour navigation
    Connections {
        target: gpsTracker
        function onPositionUpdated() {
            // Pendant un enregistrement daemon, la position vient du daemon
            // (voir Connections ci-dessous) : on ne centre pas depuis les 2 sources.
            // Premier lancement (pas de vue restaurée) : centrer sur le
            // premier fix GPS, sauf si l'utilisateur a déjà déplacé la carte.
            if (gpsTracker.hasFix && !daemonController.recording && !viewState.restored && !page.userMovedMap) {
                page.restoringView = true
                map.centerLat = gpsTracker.latitude
                map.centerLon = gpsTracker.longitude
                page.restoringView = false
            }
            if (page.followGps && gpsTracker.hasFix && !daemonController.recording) {
                map.centerLat = gpsTracker.latitude
                map.centerLon = gpsTracker.longitude
            }
            if (brouter.navigation && gpsTracker.hasFix)
                brouter.updatePosition(gpsTracker.latitude, gpsTracker.longitude)
        }
    }

    // Pendant l'enregistrement, le suivi de carte utilise la position publiée
    // par le daemon dans recording.json (une seule source GPS ouverte).
    Connections {
        target: daemonController
        function onStatsChanged() {
            if (page.followGps && daemonController.recording && daemonController.hasFix) {
                map.centerLat = daemonController.latitude
                map.centerLon = daemonController.longitude
            }
            // Départ manuel proposé (6 min sans fix) : vibration d'alerte
            // (le daemon joue aussi un beep long, même écran éteint).
            if (daemonController.recording && daemonController.awaitManualStart
                    && !page.manualStartWarned) {
                page.manualStartWarned = true
                haptics.vibrate(300)
            }
            if (!daemonController.recording)
                page.manualStartWarned = false
        }
    }

    // Tracé enregistré : ajout incrémental via pointAdded (O(1) par point GPS),
    // Tracé live sur la carte pendant l'enregistrement : désactivé avec le
    // daemon GPS (le daemon écrit un JSON, pas de signal point-by-point).
    // Le tracé est visible après l'arrêt via l'historique.
    // TODO: lire tracks/en-cours.gpx périodiquement pour le tracé live.
    /*
    Connections {
        target: routeRecorder
        function onPointAdded(point) {
            map.appendTrackPoint(point)
        }
        function onPointsChanged() {
            if (routeRecorder.pointCount === 0)
                map.trackPoints = []
        }
    }
    */

    // Pour charger un fichier GPX via le Content Hub (import).
    // handler: Source = on cherche une app qui FOURNIT le fichier (file manager).
    // contentType: All = les fichiers .gpx ne sont pas toujours reconnus "Documents".
    property var activeTransfer: null

    ContentPeerPicker {
        id: gpxPicker
        anchors.fill: parent
        visible: false
        contentType: ContentType.All
        handler: ContentHandler.Source
        showTitle: false

        onPeerSelected: {
            var transfer = peer.request()
            if (transfer) {
                page.activeTransfer = transfer
                transferTimeout.restart()
                // Ferme le picker immédiatement : le transfert tourne en
                // arrière-plan, on recevra le résultat via Connections.
                gpxPicker.visible = false
            } else {
                showToast(i18n.tr("Impossible de lancer le transfert"))
                gpxPicker.visible = false
            }
        }
        onCancelPressed: {
            gpxPicker.visible = false
        }
    }

    // Timeout de sécurité : si le transfert ne se termine pas en 30 s,
    // on abandonne (évite de rester bloqué avec activeTransfer non null).
    Timer {
        id: transferTimeout
        interval: 30000
        onTriggered: {
            if (page.activeTransfer) {
                showToast(i18n.tr("Échec du chargement du GPX"))
                page.activeTransfer = null
            }
        }
    }

    Connections {
        target: page.activeTransfer
        function onStateChanged() {
            var t = page.activeTransfer
            if (!t) return
            if (t.state === ContentTransfer.Charged) {
                transferTimeout.stop()
                if (t.items.length > 0) {
                    brouter.loadGpx(t.items[0].url.toString())
                    showToast(i18n.tr("GPX chargé"))
                } else {
                    showToast(i18n.tr("Aucun fichier reçu"))
                }
                page.activeTransfer = null
            } else if (t.state === ContentTransfer.Aborted
                    || t.state === ContentTransfer.Error) {
                transferTimeout.stop()
                showToast(i18n.tr("Échec du chargement du GPX"))
                page.activeTransfer = null
            }
        }
    }
}

