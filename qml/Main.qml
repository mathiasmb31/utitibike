
import QtQuick 2.12
import Ubuntu.Components 1.3
import "components"

MainView {
    id: root
    objectName: "mainView"
    applicationName: "utitibike"
    automaticOrientation: false
    anchorToKeyboard: false

    // Domaine de traduction : ce binding est évalué à la création de la racine,
    // avant les i18n.tr() des pages (qui lisent i18n.domain). Les traductions
    // QML vivent dans le contexte "utitibike" des fichiers po/utitibike_*.po.
    property string translationDomain: (i18n.domain = "utitibike", "utitibike")

    // Fond de la bande haute (zone sous les indicateurs, au-dessus du
    // PageStack) : couleur du mode de déplacement — gris clair à vélo,
    // terre en randonnée (comme le bandeau de la carte).
    Rectangle {
        anchors.fill: parent
        color: viewState.transportMode === "foot" ? Theme.headerModeFoot
                                                  : Theme.headerModeBike
    }

    Component.onCompleted: {
        // Le thème de l'app suit la préférence persistée (auto/sombre/clair)
        Theme.mode = Qt.binding(function() { return viewState.themeMode })
        // Source GPS persistée (auto/satellite/réseau) appliquée au tracker UI
        gpsTracker.setPositioningMode(viewState.positioningMode)
        // Préférence persistée « éviter les chemins non goudronnés » appliquée
        // au client BRouter (le Switch de MapPage écrit ensuite viewState).
        brouter.setAvoidUnpaved(viewState.avoidUnpaved)
    }

    PageStack {
        id: pageStack
        anchors.fill: parent
        // Insets « safe area » : hors des cutouts de l'écran (poinçon caméra).
        // deviceInfo détecte le poinçon par heuristique écran (ratio
        // hauteur/largeur ≥ 2,0) — aucun read_path AppArmor, donc pas de
        // redflag au click review. Sans écran allongé : marges nulles.
        anchors.topMargin: deviceInfo.safeTop
        anchors.bottomMargin: deviceInfo.safeBottom
        anchors.leftMargin: deviceInfo.safeLeft
        anchors.rightMargin: deviceInfo.safeRight
        Component.onCompleted: push(Qt.resolvedUrl("MapPage.qml"))
    }

    // ── Splash screen ──────────────────────────────────────────────
    // Grand logo centré au démarrage, au-dessus de la carte qui charge
    // en arrière-plan. Disparaît en fondu après 1,5 s (ou au tap).
    Rectangle {
        id: splash
        anchors.fill: parent
        color: Theme.bgPage   // même couleur que X-Ubuntu-Splash-Color
        z: 100

        // Positionnement explicite (x/y + bindings) — les anchors sur ids
        // siblings échouent à la création du composant (parent/id null).
        Item {
            id: splashContent
            anchors.fill: parent

            property real logoSize: Math.min(splash.width * 0.65, 768)
            property real cx: width / 2
            property real cy: height / 2 - units.gu(5)

            Image {
                id: splashLogo
                source: "qrc:/logo.png"
                x: splashContent.cx - width / 2
                y: splashContent.cy - height / 2
                width: splashContent.logoSize
                height: width
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            // Anneau de texte circulaire tournant autour du logo
            // (20 s/tour, comme l'ancien SVG du splash).
            Canvas {
                id: ringText
                width: splashContent.logoSize * 1.32
                height: width
                x: splashContent.cx - width / 2
                y: splashContent.cy - height / 2
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)
                    var text = "UT WHEELTREK · CONÇU POUR UBUNTU TOUCH · "
                    var r = width / 2 - width * 0.055
                    ctx.fillStyle = Theme.accentOrange
                    ctx.font = "bold " + Math.round(width * 0.042) + "px Ubuntu"
                    ctx.textAlign = "center"
                    ctx.textBaseline = "middle"
                    var n = text.length
                    for (var i = 0; i < n; i++) {
                        var a = (i / n) * 2 * Math.PI - Math.PI / 2
                        ctx.save()
                        ctx.translate(width / 2 + r * Math.cos(a),
                                      height / 2 + r * Math.sin(a))
                        ctx.rotate(a + Math.PI / 2)
                        ctx.fillText(text.charAt(i), 0, 0)
                        ctx.restore()
                    }
                }
                onWidthChanged: requestPaint()
                Component.onCompleted: requestPaint()
                RotationAnimation on rotation {
                    from: 0
                    to: 360
                    duration: 20000
                    loops: Animation.Infinite
                    running: splash.visible
                }
            }

            Label {
                x: splashContent.cx - width / 2
                y: splashLogo.y + splashLogo.height + units.gu(2)
                text: "UT WheelTrek"
                fontSize: "x-large"
                font.weight: Font.Bold
                color: Theme.accentOrange
            }

            Label {
                x: splashContent.cx - width / 2
                y: splashLogo.y + splashLogo.height + units.gu(6.5)
                text: i18n.tr("Version %1").arg(appVersion)
                fontSize: "medium"
                color: Theme.textSecondary
            }

            Label {
                x: splashContent.cx - width / 2
                y: splashLogo.y + splashLogo.height + units.gu(9)
                text: i18n.tr("Conçu pour Ubuntu Touch")
                fontSize: "small"
                color: Theme.accentOrange
            }
        }

        Behavior on opacity {
            NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
        }

        Timer {
            id: splashTimer
            interval: 1500
            onTriggered: splash.hide()
        }

        function hide() {
            splashTimer.stop()
            splash.opacity = 0
            destroyTimer.start()
        }

        Timer {
            id: destroyTimer
            interval: 400
            onTriggered: splash.destroy()
        }

        MouseArea {
            anchors.fill: parent
            onClicked: splash.hide()
        }

        Component.onCompleted: splashTimer.start()
    }
}

