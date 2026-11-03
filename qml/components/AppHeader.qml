import QtQuick 2.12
import Ubuntu.Components 1.3

// PageHeader au look Ubuntu Touch : fond « Ubuntu dark » #2C001E en sombre,
// blanc en clair, séparateur discret — au lieu du header par défaut.
PageHeader {
    StyleHints {
        backgroundColor: Theme.headerBg
        foregroundColor: Theme.headerFg
        dividerColor: Theme.headerDivider
    }
}
