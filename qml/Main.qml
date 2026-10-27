
import QtQuick 2.12
import Ubuntu.Components 1.3

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

    PageStack {
        id: pageStack
        anchors.fill: parent
        Component.onCompleted: push(Qt.resolvedUrl("MapPage.qml"))
    }

    // ── Splash screen ──────────────────────────────────────────────
    // Grand logo centré au démarrage, au-dessus de la carte qui charge
    // en arrière-plan. Disparaît en fondu après 1,5 s (ou au tap).
    Rectangle {
        id: splash
        anchors.fill: parent
        color: "#f7f7f7"   // même couleur que X-Ubuntu-Splash-Color
        z: 100

        Column {
            id: splashContent
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -units.gu(5)
            spacing: units.gu(2)

            Image {
                id: splashLogo
                source: "qrc:/logo.png"
                width: Math.min(splash.width * 0.65, 768)
                height: width
                fillMode: Image.PreserveAspectFit
                smooth: true
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Label {
                text: "Utitibike"
                fontSize: "x-large"
                font.weight: Font.Bold
                color: "#1278d0"
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Label {
                text: i18n.tr("Version %1").arg(appVersion)
                fontSize: "medium"
                color: "#888888"
                anchors.horizontalCenter: parent.horizontalCenter
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

