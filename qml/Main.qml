
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
}

