import QtQuick 2.12
import Ubuntu.Components 1.3
import Utitibike 1.0
import "components"

// Affiche le profil d'altitude d'un itinéraire sous forme de courbe remplie.
// Entrée : points = [{lat, lon, ele}, ...] (ele en mètres, optionnel).
// La distance cumulée (haversine, via le singleton GeoUtils) sert d'axe X,
// l'altitude d'axe Y.
// Thème « Urban Sport » : fond transparent (le SportCard parent gère le fond),
// courbe lissée (Bézier), dégradé orange, texte gris clair.
Item {
    id: root
    property var points: []
    property real distanceM: 0   // distance totale (pour l'échelle X)
    property real minEle: 0
    property real maxEle: 0
    // v0.99.1: vitesse (GPX horodaté) — courbe verte au-dessus de la
    // moyenne, ambre en dessous. hasSpeed=false pour les tracés BRouter.
    property bool hasSpeed: false
    property real avgSpeedKmh: 0
    property real ascentM: 0
    property real descentM: 0
    property real maxSlopePct: 0
    function speedAt(i) {
        return (canvas.speeds && i >= 0 && i < canvas.speeds.length)
               ? canvas.speeds[i] : -1
    }
    // v0.99.1: scrubbing — index du point sous le doigt (-1 = aucun).
    property int scrubIndex: -1
    onScrubIndexChanged: canvas.requestPaint()

    function scrubAt(px) {
        var d = canvas.distances
        if (!d || d.length < 2 || root.distanceM <= 0)
            return
        var padL = units.gu(4.5)
        var padR = units.gu(1)
        var w = width - padL - padR
        var f = Math.max(0, Math.min(1, (px - padL) / w))
        var target = f * root.distanceM
        var best = 0
        var bd = Infinity
        for (var i = 0; i < d.length; ++i) {
            var diff = Math.abs(d[i] - target)
            if (diff < bd) { bd = diff; best = i }
        }
        scrubIndex = best
    }

    implicitHeight: units.gu(12)

    function compute() {
        if (points.length < 2) {
            root.distanceM = 0
            root.minEle = 0
            root.maxEle = 0
            root.hasSpeed = false
            root.avgSpeedKmh = 0
            root.ascentM = 0
            root.descentM = 0
            root.maxSlopePct = 0
            canvas.speeds = []
            canvas.hasSpeed = false
            canvas.requestPaint()
            return
        }
        // Distance cumulée, min/max altitude et vitesse par segment
        var cum = [0]
        var speeds = [-1]
        var minE = Infinity
        var maxE = -Infinity
        var hasEle = false
        var timeS = 0
        var prevEle = null, eleRef = 0, ascent = 0, descent = 0, maxSlope = 0
        for (var i = 0; i < points.length; i++) {
            var p = points[i]
            if (p.ele !== undefined && p.ele !== null) {
                hasEle = true
                if (p.ele < minE) minE = p.ele
                if (p.ele > maxE) maxE = p.ele
                if (prevEle !== null) {
                    // Hystérésis 5 m (comme le daemon) pour D+/D- comparables.
                    if (eleRef === 0) eleRef = p.ele
                    if (p.ele - eleRef >= 5) {
                        ascent += p.ele - eleRef
                        eleRef = p.ele
                    } else if (eleRef - p.ele >= 5) {
                        descent += eleRef - p.ele
                        eleRef = p.ele
                    }
                }
                prevEle = p.ele
            }
            if (i > 0) {
                var prev = points[i - 1]
                var dd = GeoUtils.haversine(prev.lat, prev.lon, p.lat, p.lon)
                cum.push(cum[i - 1] + dd)
                if (dd > 0.5 && p.ele !== undefined && prev.ele !== undefined)
                    maxSlope = Math.max(maxSlope, Math.abs(p.ele - prev.ele) / dd * 100)
                var v = -1
                if (typeof p.t === "number" && typeof prev.t === "number") {
                    var dt = (p.t - prev.t) / 1000
                    if (dt > 0.1 && dd > 0) {
                        v = dd / dt * 3.6
                        // GPS 3 Hz : lissage leger pour eviter des pics
                        // de vitesse aberrants (200 km/h) sur la courbe.
                        var pv = speeds[i - 1]
                        if (pv > 0) v = 0.85 * v + 0.15 * pv
                        timeS += dt
                    }
                }
                speeds.push(v)
            }
        }
        root.distanceM = cum[cum.length - 1]
        root.ascentM = ascent
        root.descentM = descent
        root.maxSlopePct = maxSlope
        if (hasEle) {
            root.minEle = minE
            root.maxEle = maxE
        } else {
            root.minEle = 0
            root.maxEle = 0
        }
        root.hasSpeed = timeS > 0 && root.distanceM > 0
        root.avgSpeedKmh = root.hasSpeed ? root.distanceM / timeS * 3.6 : 0
        canvas.distances = cum
        canvas.hasEle = hasEle
        canvas.speeds = speeds
        canvas.hasSpeed = root.hasSpeed
        canvas.requestPaint()
    }

    onPointsChanged: compute()

    Canvas {
        id: canvas
        anchors.fill: parent
        property var distances: []
        property var speeds: []
        property bool hasEle: false
        property bool hasSpeed: false

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (root.points.length < 2 || distances.length < 2) return

            var padL = units.gu(4.5)
            var padR = units.gu(1)
            var padT = units.gu(0.5)
            var padB = units.gu(2)
            var w = width - padL - padR
            var h = height - padT - padB

            // Échelles
            var totalDist = root.distanceM
            var eleRange = root.maxEle - root.minEle
            if (eleRange < 1) eleRange = 1
            // Marge de 10% sur l'altitude
            var eleMin = root.minEle - eleRange * 0.1
            var eleMax = root.maxEle + eleRange * 0.1
            eleRange = eleMax - eleMin

            function xAt(i) {
                return padL + (distances[i] / totalDist) * w
            }
            function yAt(ele) {
                return padT + h - ((ele - eleMin) / eleRange) * h
            }
            // v0.99.1: 3 niveaux contrastés (vert rapide / ambre moyen /
            // rouge lent) pour que les écarts de vitesse se VOIENT sur un
            // profil de quelques dizaines de pixels de haut.
            function speedColor(v, alpha) {
                if (!(v >= 0) || !(root.avgSpeedKmh > 0))
                    return "rgba(255,107,53," + alpha + ")"        // orange par défaut
                if (v >= root.avgSpeedKmh * 1.15)
                    return "rgba(76,175,80," + alpha + ")"         // vert
                if (v < root.avgSpeedKmh * 0.85)
                    return "rgba(229,57,53," + alpha + ")"         // rouge
                return "rgba(247,147,30," + alpha + ")"            // ambre
            }

            if (hasEle) {
                // Points projetés — sous-échantillonnage à ~700 segments :
                // un GPX de 51 895 points produisait 100 000 operations
                // canvas a chaque repaint (scrubbing => saccades).
                var xs = []
                var ys = []
                var ss = []
                var step = Math.max(1, Math.floor(root.points.length / 700))
                for (var i = 0; i < root.points.length; i += step) {
                    var p = root.points[i]
                    var ele = (p.ele !== undefined && p.ele !== null) ? p.ele : eleMin
                    xs.push(xAt(i))
                    ys.push(yAt(ele))
                    ss.push(canvas.speeds[i])
                }
                if ((root.points.length - 1) % step !== 0) {
                    var last = root.points[root.points.length - 1]
                    xs.push(xAt(root.points.length - 1))
                    ys.push(yAt(last.ele !== undefined ? last.ele : eleMin))
                    ss.push(canvas.speeds[root.points.length - 1])
                }

                // Chemin lissé : quadraticCurveTo entre les milieux des
                // segments (courbe de Bézier à tangente continue).
                // depuisBase=true : part de la ligne de base et referme le
                // contour (pour le remplissage sous la courbe).
                function courbePath(depuisBase) {
                    if (depuisBase)
                        ctx.moveTo(xs[0], padT + h)
                    ctx.lineTo(xs[0], ys[0])
                    for (var i = 1; i < xs.length; i++) {
                        var midX = (xs[i - 1] + xs[i]) / 2
                        var midY = (ys[i - 1] + ys[i]) / 2
                        ctx.quadraticCurveTo(xs[i - 1], ys[i - 1], midX, midY)
                    }
                    ctx.lineTo(xs[xs.length - 1], ys[xs.length - 1])
                    if (depuisBase) {
                        ctx.lineTo(xs[xs.length - 1], padT + h)
                        ctx.closePath()
                    }
                }

                if (canvas.hasSpeed && root.avgSpeedKmh > 0) {
                    // v0.99.1: TOUTE l'aire sous la courbe est coloree par
                    // vitesse (bandes verticales), pas seulement un trait
                    // fin — sinon les ecarts etaient invisibles a cette
                    // echelle. Opacite 0.55 pour laisser lire les axes.
                    for (var s = 1; s < xs.length; s++) {
                        var sp = ss[s]
                        ctx.beginPath()
                        ctx.moveTo(xs[s - 1], padT + h)
                        ctx.lineTo(xs[s - 1], ys[s - 1])
                        ctx.lineTo(xs[s], ys[s])
                        ctx.lineTo(xs[s], padT + h)
                        ctx.closePath()
                        ctx.fillStyle = speedColor(sp, 0.55)
                        ctx.fill()
                    }
                    ctx.lineWidth = 3.5
                    for (var t = 1; t < xs.length; t++) {
                        ctx.beginPath()
                        ctx.moveTo(xs[t - 1], ys[t - 1])
                        ctx.lineTo(xs[t], ys[t])
                        ctx.strokeStyle = speedColor(ss[t], 1.0)
                        ctx.stroke()
                    }
                } else {
                    // Remplissage : dégradé vertical orange (accentOrange) → transparent
                    ctx.beginPath()
                    courbePath(true)
                    var grad = ctx.createLinearGradient(0, padT, 0, padT + h)
                    grad.addColorStop(0, "rgba(255, 107, 53, 0.85)")   // #ff6b35
                    grad.addColorStop(1, "rgba(255, 107, 53, 0.05)")
                    ctx.fillStyle = grad
                    ctx.fill()
                    // contour unique des tracés sans horodatage (BRouter)
                    ctx.beginPath()
                    courbePath(false)
                    ctx.strokeStyle = "#f7931e"
                    ctx.lineWidth = 2
                    ctx.stroke()
                }
            } else {
                // Pas d'altitude : ligne plate en pointillés au milieu
                ctx.beginPath()
                ctx.moveTo(padL, padT + h / 2)
                ctx.lineTo(padL + w, padT + h / 2)
                ctx.strokeStyle = "#555566"   // Theme.textMuted
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.stroke()
                ctx.setLineDash([])
            }

            // v0.99.1: curseur de scrubbing.
            if (scrubIndex >= 0 && scrubIndex < distances.length) {
                var sx = xAt(scrubIndex)
                ctx.strokeStyle = "#ffffff"
                ctx.globalAlpha = 0.6
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(sx, padT)
                ctx.lineTo(sx, height - padB)
                ctx.stroke()
                ctx.globalAlpha = 1.0
            }

            // Axes et labels — littéraux (le contexte 2d du Canvas prend des
            // chaînes ; les valeurs égales au thème sont rappelées en commentaire)
            ctx.font = " " + units.gu(1.2) + "px sans-serif"

            // Distance totale en bas à droite — Theme.textSecondary #888899
            ctx.fillStyle = "#888899"
            ctx.textAlign = "right"
            ctx.fillText(Format.num(totalDist / 1000) + " km", width - padR, height - units.gu(0.3))

            if (hasEle) {
                // Altitude max en haut à gauche — Theme.textPrimary #e8e8e8
                ctx.fillStyle = "#e8e8e8"
                ctx.textAlign = "left"
                ctx.fillText(Math.round(root.maxEle) + " m", padL, padT + units.gu(1.2))
                // Altitude min en bas à gauche — Theme.textSecondary #888899
                ctx.fillStyle = "#888899"
                ctx.fillText(Math.round(root.minEle) + " m", padL, height - padB - units.gu(0.3))
                // D+ / D- / pente max au centre en haut — Theme.textPrimary #e8e8e8
                ctx.fillStyle = "#e8e8e8"
                ctx.textAlign = "center"
                ctx.fillText("D+ " + Math.round(root.ascentM) + " · D- "
                             + Math.round(root.descentM) + " m · "
                             + Math.round(root.maxSlopePct) + "%",
                             padL + w / 2, padT + units.gu(1.2))
            }
        }
    }

    Label {
        anchors {
            right: parent.right
            top: parent.top
            topMargin: units.gu(0.2)
            rightMargin: units.gu(1)
        }
        visible: root.hasSpeed
        fontSize: "x-small"
        color: Theme.textPrimary
        text: i18n.tr("Vitesse moy. %1 km/h").arg(Format.num(root.avgSpeedKmh))
    }

    Row {
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: units.gu(4.5)
            bottomMargin: units.gu(0.2)
        }
        visible: root.hasSpeed
        spacing: units.gu(0.5)
        Rectangle {
            width: units.gu(0.9)
            height: width
            radius: width / 2
            color: "#4caf50"
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: i18n.tr("> moyenne")
            fontSize: "x-small"
            color: Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Rectangle {
            width: units.gu(0.9)
            height: width
            radius: width / 2
            color: "#e53935"
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: i18n.tr("< moyenne")
            fontSize: "x-small"
            color: Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: root.scrubAt(mouseX)
        onPressed: root.scrubAt(mouseX)
        onExited: root.scrubIndex = -1
    }
    Label {
        visible: root.scrubIndex >= 0 && canvas.hasEle
                 && root.scrubIndex < root.points.length
        x: Math.max(0, Math.min(parent.width - width,
                                xAtScreen(root.scrubIndex) - width / 2))
        y: units.gu(0.2)
        fontSize: "x-small"
        color: Theme.textPrimary
        text: (root.scrubIndex >= 0
               && root.scrubIndex < canvas.distances.length
               && root.scrubIndex < root.points.length
               && root.points[root.scrubIndex] !== undefined)
              ? Format.num(canvas.distances[root.scrubIndex] / 1000)
                + " km"
                + (root.points[root.scrubIndex].ele !== undefined
                   ? " · " + Math.round(root.points[root.scrubIndex].ele) + " m" : "")
                + (root.speedAt(root.scrubIndex) >= 0
                   ? " · " + Format.num(root.speedAt(root.scrubIndex)) + " km/h" : "")
              : ""
    }
    function xAtScreen(i) {
        var padL = units.gu(4.5)
        var padR = units.gu(1)
        return padL + (canvas.distances[i] / root.distanceM) * (width - padL - padR)
    }

    Label {
        anchors.centerIn: parent
        visible: root.points.length < 2
        text: i18n.tr("Pas de profil d'altitude")
        fontSize: "small"
        color: Theme.textMuted
    }
}
