
import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Content 1.3

Page {
    id: exportPage
    anchors.fill: parent
    title: i18n.tr("Enregistrer le GPX")

    property string fileUrl: ""
    property string fileName: ""
    property string handlerName: "Destination"
    property var activeTransfer: null

    ContentPeerPicker {
        id: peerPicker
        anchors {
            fill: parent
            topMargin: exportPage.header ? exportPage.header.height : 0
        }
        contentType: ContentType.All
        handler: exportPage.handlerName === "Share" ? ContentHandler.Share : ContentHandler.Destination
        showTitle: false

        onPeerSelected: {
            var transfer = peer.request()
            if (transfer) {
                // Propriétés assignées après création : pas de concaténation
                // de chaînes dans le QML source (injection, quoting cassé).
                var item = Qt.createQmlObject(
                    "import Ubuntu.Content 1.3; ContentItem { }", exportPage)
                item.url = exportPage.fileUrl
                item.name = exportPage.fileName
                transfer.items = [item]
                transfer.state = ContentTransfer.Charged
                exportPage.activeTransfer = transfer
            }
        }
        onCancelPressed: pageStack.pop()
    }

    Connections {
        target: exportPage.activeTransfer
        function onStateChanged() {
            if (exportPage.activeTransfer.state === ContentTransfer.Completed
                    || exportPage.activeTransfer.state === ContentTransfer.Aborted) {
                pageStack.pop()
            }
        }
    }
}

