
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

    // Fond du bandeau système (indicateurs + poinçon caméra physique) :
    // blanc comme le PageHeader pour que les indicateurs du shell restent
    // lisibles. Le contenu des pages démarre SOUS la zone coupée (voir les
    // marges du PageStack ci-dessous) — le poinçon ne masque plus rien.
    Rectangle {
        anchors.fill: parent
        color: "#ffffff"
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

                // Rotation du texte circulaire — même vitesse que le SVG (20s/tour)
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
                text: "Utitibike"
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

