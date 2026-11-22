pragma Singleton
import QtQuick 2.12

// v0.99.1: formatage centralise respectant la locale.
// ATTENTION: Qt.locale().toString(x, 'f', n) resout vers Object.toString()
// en QML 5.12 -> "[object Object]". Utiliser Number.toLocaleString() et
// Qt.formatDate/formatDateTime, qui renvoient de vraies chaines.
QtObject {
    function num(value, digits) {
        var d = digits === undefined ? 1 : digits
        return Number(value).toLocaleString(Qt.locale(), 'f', d)
    }
    function date(value) {
        return Qt.formatDate(new Date(value), Qt.DefaultLocaleShortDate)
    }
}
