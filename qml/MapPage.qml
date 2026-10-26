
import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Content 1.3
import Utitibike 1.0

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

    readonly property var profiles: ["trekking", "fastbike", "safety", "shortest"]

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

    // En second plan, plus aucun téléchargement de tuile :
    // l'appli ne dépense pratiquement rien en arrière-plan.
    TileCache { id: cache; paused: !appState.foreground }

    BrouterClient {
        id: brouter
        profile: page.profiles[page.profileIndex]
        onRouteReady: {
            page.routePoints = points
            page.routeDistance = distanceM
            page.routeAscent = ascentM
            page.routeGpx = brouter.lastGpx
            showToast(i18n.tr("Itinéraire calculé"))
            // DEBUG temporaire : détecter un point de route très loin des waypoints
            // (> 100 km du waypoint le plus proche = point fantôme dans la route)
            function haversine(lat1, lon1, lat2, lon2) {
                var r = 6371000, dLat = (lat2 - lat1) * Math.PI / 180
                var dLon = (lon2 - lon1) * Math.PI / 180
                var a = Math.sin(dLat/2) * Math.sin(dLat/2)
                      + Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180)
                        * Math.sin(dLon/2) * Math.sin(dLon/2)
                return 2 * r * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
            }
            for (var i = 0; i < points.length; i++) {
                var p = points[i]
                var minDist = Infinity
                for (var j = 0; j < page.waypoints.length; j++) {
                    var w = page.waypoints[j]
                    var d = haversine(p.lat, p.lon, w.lat, w.lon)
                    if (d < minDist) minDist = d
                }
                if (minDist > 100000) {
                    showToast("Route: point #" + i + " à " + Math.round(minDist/1000)
                              + " km — " + p.lat.toFixed(5) + "," + p.lon.toFixed(5))
                    break
                }
            }
            // Sauvegarde automatique dans l'historique
            routeHistory.addEntry("", distanceM, ascentM, page.waypoints, brouter.lastGpx)
        }
        onRouteFailed: {
            page.routePoints = []
            page.routeGpx = ""
            showToast(message)
        }
        // Alertes virage : vibration à 200m, 100m, 50m
        onTurnAlert: {
            if (severity === 0) haptics.vibrate(30)
            else if (severity === 1) haptics.vibrate(60)
            else haptics.vibrate(100)
        }
    }

    TileMapItem {
        id: map
        anchors.fill: parent
        cache: cache
        waypoints: page.waypoints
        route: page.routePoints
        currentPosition: gpsTracker.hasFix ? { "lat": gpsTracker.latitude, "lon": gpsTracker.longitude } : ({})
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
        color: "#666"
        style: Text.Outline
        styleColor: "white"
    }

    // Indicateur hors ligne (auto-détection réseau dans TileCache : 3 échecs
    // réseau consécutifs -> hors ligne, sonde toutes les 30 s pour le retour)
    Rectangle {
        z: 15
        anchors {
            top: parent.top
            right: parent.right
            margins: units.gu(1)
            topMargin: units.gu(8)
        }
        visible: !cache.online
        color: "#c0392b"
        radius: units.gu(0.5)
        width: offlineLabel.width + units.gu(2)
        height: offlineLabel.height + units.gu(1)
        Label {
            id: offlineLabel
            anchors.centerIn: parent
            text: i18n.tr("Hors ligne")
            color: "white"
            fontSize: "x-small"
            font.bold: true
        }
    }

    // Bandeau de navigation turn-by-turn (visible en navigation)
    Rectangle {
        id: navBanner
        z: 10
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: navCol.height + units.gu(1.5)
        color: "#1278d0"
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
                margins: units.gu(1)
            }
            spacing: units.gu(0.3)
            Label {
                width: parent.width
                color: "white"
                fontSize: "medium"
                font.bold: true
                wrapMode: Text.Wrap
                text: brouter.nextInstruction || "…"
            }
            Label {
                color: "#c0dcff"
                fontSize: "small"
                text: brouter.nextInstructionDistance > 0
                      ? i18n.tr("dans %1 m").arg(Math.round(brouter.nextInstructionDistance))
                      : ""
            }
        }
    }

    // Profil d'altitude en overlay en haut de l'écran (visible quand un
    // itinéraire est calculé). Se place sous le bandeau de navigation.
    Rectangle {
        id: elevationOverlay
        z: 5
        anchors {
            left: parent.left
            right: parent.right
            leftMargin: units.gu(1)
            rightMargin: units.gu(1)
        }
        y: navBanner.visible ? navBanner.height + units.gu(2) : units.gu(1)
        height: units.gu(6)
        color: "#f7f7f7"
        opacity: 0.92
        radius: units.gu(0.5)
        visible: page.routePoints.length > 1
        Behavior on y { NumberAnimation { duration: 250 } }
        Behavior on opacity { NumberAnimation { duration: 150 } }

        ElevationProfile {
            anchors.fill: parent
            anchors.margins: units.gu(0.5)
            points: page.routePoints
        }
    }

    // Tableau de bord (overlay bas-gauche, visible si GPS actif ou enregistrement)
    Rectangle {
        id: dashboard
        z: 10
        x: units.gu(1)
        y: parent.height - height - (page.panelOpen ? panel.height + units.gu(2) : units.gu(2))
        width: dashCol.width + units.gu(2)
        height: dashCol.height + units.gu(1)
        radius: units.gu(0.8)
        color: "#e6262626"
        visible: gpsTracker.active || routeRecorder.recording
        Behavior on y { NumberAnimation { duration: 200 } }

        Column {
            id: dashCol
            anchors.centerIn: parent
            spacing: units.gu(0.2)
            Row {
                spacing: units.gu(1.5)
                Label {
                    color: "white"
                    fontSize: "medium"
                    font.bold: true
                    text: gpsTracker.hasFix ? gpsTracker.speedKmh.toFixed(1) : "--"
                }
                Label {
                    color: "#aaa"
                    fontSize: "small"
                    text: "km/h"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Label {
                color: "#ddd"
                fontSize: "small"
                visible: routeRecorder.recording
                text: (routeRecorder.distanceM / 1000).toFixed(2) + " km"
                    + "  ·  D+ " + Math.round(routeRecorder.ascentM) + " m"
                    + "  ·  " + formatDuration(routeRecorder.elapsedSec)
            }
            Label {
                color: "#aaa"
                fontSize: "x-small"
                text: gpsTracker.statusText
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
    Rectangle {
        id: searchBar
        z: 20
        anchors {
            left: parent.left
            right: parent.right
            margins: units.gu(1)
        }
        y: navBanner.visible ? navBanner.height + units.gu(1.5) : units.gu(1)
        height: searchCol.height + units.gu(1)
        radius: units.gu(0.6)
        color: "#f7f7f7"
        border.color: "#d2d2d2"
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
                    width: parent.width - units.gu(5)
                    placeholderText: i18n.tr("Rechercher un lieu…")
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onAccepted: page.doSearch(text)
                    Keys.onReturnPressed: page.doSearch(text)
                    Keys.onEnterPressed: page.doSearch(text)
                }
                Button {
                    width: units.gu(4)
                    height: units.gu(4)
                    text: "→"
                    color: UbuntuColors.blue
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
                    color: index % 2 === 0 ? "#ffffff" : "#f0f0f0"
                    Label {
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            margins: units.gu(0.5)
                        }
                        fontSize: "small"
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

    function doSearch(query) {
        if (query.length < 3) return
        page.searchBusy = true
        page.searchResults = []
        var xhr = new XMLHttpRequest()
        // Recherche mondiale (pas de countrycodes=fr en dur : l'app n'est pas
        // limitée à la France).
        var url = "https://nominatim.openstreetmap.org/search?format=json&limit=8&q="
                  + encodeURIComponent(query)
        xhr.open("GET", url)
        // Nominatim exige un User-Agent identifiable (sinon HTTP 403)
        xhr.setRequestHeader("User-Agent", "utitibike/" + appVersion + " (Ubuntu Touch; contact: user@localhost)")
        xhr.setRequestHeader("Accept", "application/json")
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                page.searchBusy = false
                if (xhr.status === 200) {
                    try {
                        var results = JSON.parse(xhr.responseText)
                        page.searchResults = results.map(function(r) {
                            return { lat: parseFloat(r.lat), lon: parseFloat(r.lon), display_name: r.display_name }
                        })
                        if (page.searchResults.length === 0)
                            showToast(i18n.tr("Aucun résultat"))
                    } catch (e) {
                        showToast(i18n.tr("Erreur de recherche"))
                    }
                } else {
                    showToast(i18n.tr("Recherche indisponible (HTTP %1)").arg(xhr.status))
                }
            }
        }
        xhr.send()
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
            }
            onCanceled: {
                warnTimer.stop()
                addTimer.stop()
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
        Button {
            width: units.gu(5)
            height: units.gu(5)
            text: "+"
            onClicked: map.zoomAbout(1, map.width / 2, map.height / 2)
        }
        Button {
            width: units.gu(5)
            height: units.gu(5)
            text: "–"
            onClicked: map.zoomAbout(-1, map.width / 2, map.height / 2)
        }
    }

    ActivityIndicator {
        anchors.centerIn: parent
        running: brouter.busy
        visible: brouter.busy
    }

    Label {
        id: hint
        anchors {
            top: parent.top
            topMargin: units.gu(8)
            horizontalCenter: parent.horizontalCenter
        }
        text: i18n.tr("Appui long sur la carte : ajouter un point")
        visible: page.waypoints.length === 0
        color: "#444444"
        fontSize: "small"
    }

    Rectangle {
        id: toastRect
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
        }
        anchors.bottomMargin: page.panelOpen ? panel.height + units.gu(2) : units.gu(2)
        width: Math.min(parent.width - units.gu(4), toastLabel.width + units.gu(2))
        height: toastLabel.height + units.gu(1)
        radius: units.gu(0.5)
        color: "#262626"
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: "white"
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
        color: "#f7f7f7"
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
                }

                Rectangle {
                    width: parent.width
                    height: Math.min(units.gu(16), units.gu(3.4) * page.waypoints.length + units.gu(1))
                    color: "white"
                    radius: units.gu(0.4)
                    border.color: "#d2d2d2"
                    visible: page.waypoints.length > 0
                    ListView {
                        id: wpList
                        anchors.fill: parent
                        anchors.margins: units.gu(0.5)
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
                                text: (index === 0 ? i18n.tr("Départ") : (index === page.waypoints.length - 1 ? i18n.tr("Arrivée") : i18n.tr("Étape %1").arg(index)))
                                      + "  ·  %1, %2".arg(modelData.lat.toFixed(5)).arg(modelData.lon.toFixed(5))
                            }
                            Button {
                                width: units.gu(5)
                                height: units.gu(5)
                                anchors.verticalCenter: parent.verticalCenter
                                color: UbuntuColors.red
                                onClicked: page.removeWaypoint(index)
                                Icon {
                                    anchors.centerIn: parent
                                    name: "delete"
                                    width: units.gu(2.5)
                                    height: units.gu(2.5)
                                    color: "white"
                                }
                            }
                        }
                    }
                }

                Label {
                    text: i18n.tr("Profil BRouter")
                    fontSize: "small"
                    font.bold: true
                }
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Repeater {
                        model: page.profiles
                        Button {
                            width: (parent.width - units.gu(1.5)) / 4
                            text: modelData
                            color: page.profileIndex === index ? UbuntuColors.orange : "#dcdcdc"
                            onClicked: page.profileIndex = index
                        }
                    }
                }

                Label {
                    text: i18n.tr("Dénivelé")
                    fontSize: "small"
                    font.bold: true
                }
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Button {
                        width: (parent.width - units.gu(1)) / 3
                        text: i18n.tr("Facile")
                        color: brouter.uphillCost >= 300 ? UbuntuColors.green : "#dcdcdc"
                        onClicked: brouter.uphillCost = 500
                    }
                    Button {
                        width: (parent.width - units.gu(1)) / 3
                        text: i18n.tr("Normal")
                        color: brouter.uphillCost >= 0 && brouter.uphillCost < 300 ? UbuntuColors.orange : "#dcdcdc"
                        onClicked: brouter.uphillCost = 50
                    }
                    Button {
                        width: (parent.width - units.gu(1)) / 3
                        text: i18n.tr("Difficile")
                        color: brouter.uphillCost < 0 ? UbuntuColors.red : "#dcdcdc"
                        onClicked: brouter.uphillCost = -100
                    }
                }
                Slider {
                    width: parent.width
                    minimumValue: -200
                    maximumValue: 500
                    stepSize: 10
                    value: brouter.uphillCost
                    onValueChanged: brouter.uphillCost = Math.round(value)
                }
                Label {
                    fontSize: "small"
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: brouter.uphillCost < 0
                          ? i18n.tr("Sportif : cherche les côtes (bonus D+ %1)").arg(-brouter.uphillCost)
                          : brouter.uphillCost === 0
                            ? i18n.tr("Ignore le dénivelé dans le calcul")
                            : brouter.uphillCost < 100
                              ? i18n.tr("Normal : pénalise légèrement les montées")
                              : i18n.tr("Facile : évite les côtes (coût %1)").arg(brouter.uphillCost)
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: "#d2d2d2"
                }

                Label {
                    text: i18n.tr("GPS & Navigation")
                    fontSize: "small"
                    font.bold: true
                }
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        text: gpsTracker.active ? i18n.tr("GPS actif") : i18n.tr("Activer GPS")
                        color: gpsTracker.active ? UbuntuColors.green : "#dcdcdc"
                        onClicked: {
                            if (gpsTracker.active) gpsTracker.stop()
                            else gpsTracker.start()
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        text: page.followGps ? i18n.tr("Suivi GPS") : i18n.tr("Centrer GPS")
                        enabled: gpsTracker.hasFix
                        color: page.followGps ? UbuntuColors.blue : "#dcdcdc"
                        onClicked: {
                            page.followGps = !page.followGps
                            if (page.followGps && gpsTracker.hasFix) {
                                map.centerLat = gpsTracker.latitude
                                map.centerLon = gpsTracker.longitude
                            }
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(1.5)) / 3
                        text: brouter.navigation ? i18n.tr("Arrêter nav.") : i18n.tr("Naviguer")
                        enabled: page.routePoints.length > 1
                        color: brouter.navigation ? UbuntuColors.red : UbuntuColors.blue
                        onClicked: {
                            if (brouter.navigation) {
                                brouter.stopNavigation()
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

                // Sémantique : « Enregistrer » démarre, « Arrêter » stoppe ET
                // sauvegarde le parcours (auto-sauvegarde toutes les 60 s en
                // plus), « Effacer tracé » efface sans sauvegarder. Pas de
                // bouton « Sauvegarder » séparé (redondant avec « Arrêter »).
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Button {
                        width: (parent.width - units.gu(0.5)) / 2
                        text: routeRecorder.recording ? i18n.tr("Arrêter") : i18n.tr("Enregistrer")
                        color: routeRecorder.recording ? UbuntuColors.red : UbuntuColors.green
                        onClicked: {
                            if (routeRecorder.recording) {
                                routeRecorder.stop()
                                var url = routeRecorder.saveGpx()
                                if (url !== "")
                                    showToast(i18n.tr("Parcours sauvegardé"))
                                routeRecorder.clear()
                            } else {
                                routeRecorder.clear()
                                gpsTracker.start()
                                routeRecorder.start()
                            }
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(0.5)) / 2
                        text: i18n.tr("Effacer tracé")
                        enabled: routeRecorder.pointCount > 0
                        onClicked: {
                            routeRecorder.clear()
                            routeRecorder.stop()
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: "#d2d2d2"
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        text: brouter.busy ? i18n.tr("Calcul…") : i18n.tr("Calculer l'itinéraire")
                        enabled: page.waypoints.length >= 2 && !brouter.busy
                        color: UbuntuColors.blue
                        onClicked: {
                            // DEBUG temporaire : afficher les coordonnées envoyées à BRouter
                            var coords = []
                            for (var i = 0; i < page.waypoints.length; i++) {
                                var w = page.waypoints[i]
                                coords.push((i + 1) + ":" + w.lat.toFixed(5) + "," + w.lon.toFixed(5))
                            }
                            showToast("BRouter ← " + coords.join(" | "))
                            brouter.requestRoute(page.waypoints)
                        }
                    }
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        text: i18n.tr("Exporter GPX")
                        enabled: page.routeGpx !== ""
                        onClicked: page.exportNow()
                    }
                }

                Button {
                    width: parent.width
                    text: i18n.tr("Partager le GPX")
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
                    text: i18n.tr("Charger un GPX")
                    onClicked: {
                        gpxPicker.visible = true
                    }
                }

                Button {
                    width: parent.width
                    text: i18n.tr("Historique (%1)").arg(routeHistory.entries.length)
                    onClicked: pageStack.push(Qt.resolvedUrl("HistoryPage.qml"))
                }

                Button {
                    width: parent.width
                    text: i18n.tr("À propos")
                    onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: "#d2d2d2"
                }

                Label {
                    text: i18n.tr("Favoris (%1)").arg(favorites.entries.length)
                    fontSize: "small"
                    font.bold: true
                }
                Button {
                    width: parent.width
                    text: i18n.tr("Ajouter ma position aux favoris")
                    enabled: gpsTracker.hasFix
                    onClicked: {
                        var name = i18n.tr("Position %1").arg(new Date().toLocaleTimeString())
                        favorites.add(name, gpsTracker.latitude, gpsTracker.longitude, "position")
                        showToast(i18n.tr("Favori ajouté"))
                    }
                }
                Repeater {
                    model: favorites.entries
                    Rectangle {
                        width: parent.width
                        height: favRow.height + units.gu(1)
                        color: "white"
                        radius: units.gu(0.4)
                        border.color: "#d2d2d2"
                        Row {
                            id: favRow
                            width: parent.width - units.gu(1)
                            anchors.centerIn: parent
                            spacing: units.gu(0.5)
                            Label {
                                width: parent.width - units.gu(10)
                                fontSize: "small"
                                elide: Text.ElideRight
                                text: modelData.name + "  ·  " + modelData.category
                            }
                            Button {
                                width: units.gu(4)
                                height: units.gu(4)
                                text: "→"
                                color: UbuntuColors.blue
                                onClicked: {
                                    map.centerLat = modelData.lat
                                    map.centerLon = modelData.lon
                                    map.zoom = Math.max(map.zoom, 15)
                                }
                            }
                            Button {
                                width: units.gu(4)
                                height: units.gu(4)
                                color: UbuntuColors.red
                                onClicked: favorites.remove(index)
                                Icon {
                                    anchors.centerIn: parent
                                    name: "delete"
                                    width: units.gu(2)
                                    height: units.gu(2)
                                    color: "white"
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: "#d2d2d2"
                }

                Label {
                    text: i18n.tr("Garmin Connect")
                    fontSize: "small"
                    font.bold: true
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    fontSize: "small"
                    text: i18n.tr("Le GPX exporté contient des horodatages : Garmin Connect l'accepte (menu « Importer des données » du site).")
                }
                Button {
                    width: parent.width
                    text: i18n.tr("Ouvrir Garmin Connect dans le navigateur")
                    onClicked: {
                        if (!urlOpener.openUrl("https://connect.garmin.com/"))
                            showToast(i18n.tr("Impossible d'ouvrir le navigateur"))
                    }
                }

                Label {
                    visible: page.routeDistance > 0
                    fontSize: "medium"
                    font.bold: true
                    text: "%1 km  ·  D+ %2 m".arg((page.routeDistance / 1000).toFixed(1)).arg(Math.round(page.routeAscent))
                }

                Rectangle {
                    width: parent.width
                    height: units.gu(0.1)
                    color: "#d2d2d2"
                }

                Label {
                    text: i18n.tr("Carte hors ligne (tuiles OpenStreetMap)")
                    fontSize: "small"
                    font.bold: true
                }
                Row {
                    width: parent.width
                    spacing: units.gu(0.5)
                    Repeater {
                        model: [
                            { key: "osm", label: i18n.tr("Standard") },
                            { key: "cyclosm", label: i18n.tr("Cyclable") },
                            { key: "dark", label: i18n.tr("Sombre") },
                            { key: "satellite", label: i18n.tr("Satellite") }
                        ]
                        Button {
                            width: (parent.width - units.gu(1.5)) / 4
                            text: modelData.label
                            color: cache.tileServer === modelData.key ? UbuntuColors.orange : "#dcdcdc"
                            onClicked: cache.tileServer = modelData.key
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        text: i18n.tr("Précharger la zone affichée")
                        onClicked: page.startDownload()
                    }
                    Button {
                        width: (parent.width - units.gu(1)) / 2
                        text: i18n.tr("Annuler")
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
                    text: i18n.tr("Tuiles : %1 / %2").arg(page.dlDone).arg(page.dlTotal)
                }
            }
        }
    }

    Button {
        id: panelToggle
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: units.gu(2)
            bottomMargin: units.gu(2)
        }
        width: units.gu(7)
        height: units.gu(7)
        color: UbuntuColors.orange
        onClicked: page.panelOpen = !page.panelOpen
        Icon {
            anchors.centerIn: parent
            name: page.panelOpen ? "close" : "navigation-menu"
            width: units.gu(3.5)
            height: units.gu(3.5)
            color: "white"
        }
    }

    header: PageHeader {
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
            // Premier lancement (pas de vue restaurée) : centrer sur le
            // premier fix GPS, sauf si l'utilisateur a déjà déplacé la carte.
            if (gpsTracker.hasFix && !viewState.restored && !page.userMovedMap) {
                page.restoringView = true
                map.centerLat = gpsTracker.latitude
                map.centerLon = gpsTracker.longitude
                page.restoringView = false
            }
            if (page.followGps && gpsTracker.hasFix) {
                map.centerLat = gpsTracker.latitude
                map.centerLon = gpsTracker.longitude
            }
            if (brouter.navigation && gpsTracker.hasFix)
                brouter.updatePosition(gpsTracker.latitude, gpsTracker.longitude)
        }
    }

    // Tracé enregistré : ajout incrémental via pointAdded (O(1) par point GPS),
    // sans copier la liste complète à chaque fix. pointsChanged sert à détecter
    // l'effacement (clear) : liste vide -> on vide le tracé.
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

