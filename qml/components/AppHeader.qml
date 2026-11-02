import QtQuick 2.12
import Ubuntu.Components 1.3

// PageHeader au look Ubuntu Touch sombre : fond « Ubuntu dark » #2C001E,
// texte blanc, séparateur discret — au lieu du header blanc par défaut.
PageHeader {
    StyleHints {
        backgroundColor: Theme.ubuntuDark
        foregroundColor: "#ffffff"
        dividerColor: "#33ffffff"
    }
}
