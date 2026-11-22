
import QtQuick 2.12
import Ubuntu.Components 1.3
import Ubuntu.Content 1.3
import "components"

Page {
    id: exportPage
    anchors.fill: parent
    header: AppHeader { title: i18n.tr("Enregistrer le GPX") }

    property string fileUrl: ""
    property string fileName: ""
    property string handlerName: "Destination"
    property var activeTransfer: null

    Label {
        id: errorLabel
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: parent.top
            topMargin: exportPage.header ? exportPage.header.height + units.gu(2) : units.gu(2)
            margins: units.gu(1)
        }
        width: parent.width - units.gu(2)
        visible: text !== ""
        wrapMode: Text.WordWrap
        color: Theme.accentRed
        fontSize: "small"
        horizontalAlignment: Text.AlignHCenter
        text: ""
    }

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
            var st = exportPage.activeTransfer.state
            if (st === ContentTransfer.Error) {
                // v0.99.1: un echec de transfert restait silencieux.
                errorLabel.text = i18n.tr("Échec du transfert du GPX")
                return
            }
            if (st === ContentTransfer.Completed
                    || st === ContentTransfer.Aborted) {
                pageStack.pop()
            }
        }
    }
}

