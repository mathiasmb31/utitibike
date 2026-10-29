import QtQuick 2.12
import Ubuntu.Components 1.3
import "components"

// Section d'aide réutilisable : titre + corps de texte
Column {
    width: parent.width
    spacing: units.gu(0.5)

    property string title: ""
    property string body: ""

    Label {
        width: parent.width
        text: title
        fontSize: "medium"
        font.bold: true
        color: Theme.accentOrange
    }
    Label {
        width: parent.width
        wrapMode: Text.Wrap
        fontSize: "small"
        color: Theme.textPrimary
        text: body
        lineHeight: 1.3
    }
    Rectangle { width: parent.width; height: units.gu(0.05); color: Theme.bgCardBorder }
}
