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

    implicitHeight: units.gu(12)

    function compute() {
        if (points.length < 2) {
            root.distanceM = 0
            root.minEle = 0
            root.maxEle = 0
            canvas.requestPaint()
            return
        }
        // Distance cumulée et min/max altitude
        var cum = [0]
        var minE = Infinity
        var maxE = -Infinity
        var hasEle = false
        for (var i = 0; i < points.length; i++) {
            var p = points[i]
            if (p.ele !== undefined && p.ele !== null) {
                hasEle = true
                if (p.ele < minE) minE = p.ele
                if (p.ele > maxE) maxE = p.ele
            }
            if (i > 0) {
                var prev = points[i - 1]
                cum.push(cum[i - 1] + GeoUtils.haversine(prev.lat, prev.lon, p.lat, p.lon))
            }
        }
        root.distanceM = cum[cum.length - 1]
        if (hasEle) {
            root.minEle = minE
            root.maxEle = maxE
        } else {
            root.minEle = 0
            root.maxEle = 0
        }
        canvas.distances = cum
        canvas.hasEle = hasEle
        canvas.requestPaint()
    }

    onPointsChanged: compute()

    Canvas {
        id: canvas
        anchors.fill: parent
        property var distances: []
        property bool hasEle: false

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

            if (hasEle) {
                // Points projetés
                var xs = []
                var ys = []
                for (var i = 0; i < root.points.length; i++) {
                    var p = root.points[i]
                    var ele = (p.ele !== undefined && p.ele !== null) ? p.ele : eleMin
                    xs.push(xAt(i))
                    ys.push(yAt(ele))
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

                // Remplissage : dégradé vertical orange (accentOrange) → transparent
                ctx.beginPath()
                courbePath(true)
                var grad = ctx.createLinearGradient(0, padT, 0, padT + h)
                grad.addColorStop(0, "rgba(255, 107, 53, 0.85)")   // #ff6b35
                grad.addColorStop(1, "rgba(255, 107, 53, 0.05)")
                ctx.fillStyle = grad
                ctx.fill()

                // Ligne de contour : ambre (accentAmber #f7931e)
                ctx.beginPath()
                courbePath(false)
                ctx.strokeStyle = "#f7931e"
                ctx.lineWidth = 2
                ctx.stroke()
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

            // Axes et labels — littéraux (le contexte 2d du Canvas prend des
            // chaînes ; les valeurs égales au thème sont rappelées en commentaire)
            ctx.font = " " + units.gu(1.2) + "px sans-serif"

            // Distance totale en bas à droite — Theme.textSecondary #888899
            ctx.fillStyle = "#888899"
            ctx.textAlign = "right"
            ctx.fillText((totalDist / 1000).toFixed(1) + " km", width - padR, height - units.gu(0.3))

            if (hasEle) {
                // Altitude max en haut à gauche — Theme.textPrimary #e8e8e8
                ctx.fillStyle = "#e8e8e8"
                ctx.textAlign = "left"
                ctx.fillText(Math.round(root.maxEle) + " m", padL, padT + units.gu(1.2))
                // Altitude min en bas à gauche — Theme.textSecondary #888899
                ctx.fillStyle = "#888899"
                ctx.fillText(Math.round(root.minEle) + " m", padL, height - padB - units.gu(0.3))
                // D+ au centre en haut — Theme.textPrimary #e8e8e8
                var dPlus = root.maxEle - root.minEle
                ctx.fillStyle = "#e8e8e8"
                ctx.textAlign = "center"
                ctx.fillText("D+ " + Math.round(dPlus) + " m", padL + w / 2, padT + units.gu(1.2))
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: root.points.length < 2
        text: i18n.tr("Pas de profil d'altitude")
        fontSize: "small"
        color: Theme.textMuted
    }
}
