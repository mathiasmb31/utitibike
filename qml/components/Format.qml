pragma Singleton
import QtQuick 2.12

// v0.99.1: formatage centralise respectant la locale (avant, .toFixed(1)
// imposait le point decimal en FR/DE et les dates etaient heterogenes).
QtObject {
    function num(value, digits) {
        return Qt.locale().toString(Number(value), 'f', digits === undefined ? 1 : digits)
    }
    function integer(value) {
        return Qt.locale().toString(Number(value), 'f', 0)
    }
    function date(value) {
        return new Date(value).toLocaleDateString(Qt.locale())
    }
    function dateTime(value) {
        return new Date(value).toLocaleString(Qt.locale(), "dd/MM HH:mm")
    }
}
