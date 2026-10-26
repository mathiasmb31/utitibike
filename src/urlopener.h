#ifndef URLOPENER_H
#define URLOPENER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtDBus>

// Opens a URL in the default browser (Morph) through Lomiri's URL dispatcher.
// Qt.openUrlExternally() does not work under confinement (no .desktop handler
// visible for https:); the default click AppArmor profile allows this exact
// D-Bus call.
class UrlOpener : public QObject
{
    Q_OBJECT
public:
    explicit UrlOpener(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool openUrl(const QString &url)
    {
        return dispatch(url, 0);
    }

private:
    // Appel D-Bus asynchrone (ne bloque pas le thread UI). Lomiri (24.04)
    // d'abord, puis l'ancien nom Canonical en cas d'erreur.
    bool dispatch(const QString &url, int index)
    {
        // The bus name uses dots (com.lomiri.URLDispatcher) but the object
        // path uses slashes (/com/lomiri/URLDispatcher).
        static const char *names[] = { "com.lomiri.URLDispatcher", "com.canonical.URLDispatcher" };
        if (index >= 2)
            return false;
        const QString n = QString::fromLatin1(names[index]);
        QString objectPath = QStringLiteral("/") + n;
        // Convert dots to slashes for the object path:
        // com.lomiri.URLDispatcher -> /com/lomiri/URLDispatcher
        objectPath.replace(QLatin1Char('.'), QLatin1Char('/'));
        QDBusMessage msg = QDBusMessage::createMethodCall(
                    n, objectPath, n, QStringLiteral("DispatchURL"));
        // (url, appPackage): an empty appPackage lets the dispatcher pick
        // the app — the browser for an https URL.
        msg.setArguments({ QVariant(url), QVariant(QString()) });
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, url, index](QDBusPendingCallWatcher *w) {
            if (w->reply().type() == QDBusMessage::ErrorMessage)
                dispatch(url, index + 1);
            w->deleteLater();
        });
        return true;
    }
};

#endif // URLOPENER_H
