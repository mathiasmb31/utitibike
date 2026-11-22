#ifndef DISPLAYKEEPER_H
#define DISPLAYKEEPER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QDebug>
#include <QtDBus>

// Keeps the display on through repowerd's Unity.Screen API. Lomiri stops
// (SIGSTOP) apps when the screen turns off, so holding this request is the
// sanctioned way for a feature that must stay active with the screen off
// (e.g. GPS recording / navigation) — it requires the standard
// "keep-display-on" AppArmor policy group. The request is held until
// release() or app exit; if the call is refused the app just runs without
// it (logged, never fatal).
class DisplayKeeper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool holding READ holding NOTIFY holdingChanged)
public:
    explicit DisplayKeeper(QObject *parent = nullptr) : QObject(parent) {}

    bool holding() const { return m_id >= 0; }

    Q_INVOKABLE void hold()
    {
        if (holding() || m_holdPending)
            return;
        m_holdPending = true;
        m_holdCancelled = false;
        QDBusMessage msg = QDBusMessage::createMethodCall(
                    QStringLiteral("com.canonical.Unity.Screen"),
                    QStringLiteral("/com/canonical/Unity/Screen"),
                    QStringLiteral("com.canonical.Unity.Screen"),
                    QStringLiteral("keepDisplayOn"));
        // Appel asynchrone : ne bloque pas le thread UI, l'ID de la requête
        // arrive dans le callback.
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                    QDBusConnection::systemBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *w) {
            m_holdPending = false;
            const QDBusMessage reply = w->reply();
            if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
                qWarning() << "displaykeeper hold FAIL" << reply.errorMessage();
            } else {
                bool ok = false;
                const int id = reply.arguments().first().toInt(&ok);
                if (!ok || id < 0) {
                    qWarning() << "displaykeeper hold FAIL bad id";
                } else if (m_holdCancelled) {
                    // release() est arrivé avant la réponse : on libère tout
                    // de suite l'ID reçu au lieu de garder l'écran allumé.
                    QDBusMessage rm = QDBusMessage::createMethodCall(
                                QStringLiteral("com.canonical.Unity.Screen"),
                                QStringLiteral("/com/canonical/Unity/Screen"),
                                QStringLiteral("com.canonical.Unity.Screen"),
                                QStringLiteral("removeDisplayOnRequest"));
                    rm.setArguments({ QVariant(id) });
                    QDBusConnection::systemBus().asyncCall(rm);
                    m_holdCancelled = false;
                } else {
                    m_id = id;
                    qWarning() << "displaykeeper holding id" << m_id;
                    emit holdingChanged();
                }
            }
            w->deleteLater();
        });
    }

    Q_INVOKABLE void release()
    {
        if (m_holdPending) {
            // La réponse n'est pas encore arrivée : le callback se chargera
            // de libérer l'ID dès qu'il sera connu.
            m_holdPending = false;
            m_holdCancelled = true;
            return;
        }
        if (!holding())
            return;
        QDBusMessage msg = QDBusMessage::createMethodCall(
                    QStringLiteral("com.canonical.Unity.Screen"),
                    QStringLiteral("/com/canonical/Unity/Screen"),
                    QStringLiteral("com.canonical.Unity.Screen"),
                    QStringLiteral("removeDisplayOnRequest"));
        msg.setArguments({ QVariant(m_id) });
        // Fire and forget : la requête keep-display-on est liée à notre
        // connexion D-Bus ; à la sortie de l'app, la connexion se ferme et
        // la requête est libérée automatiquement par le serveur. Un appel
        // async ne bloque pas le thread UI (contrairement à QDBus::Block).
        QDBusConnection::systemBus().asyncCall(msg);
        m_id = -1;
        emit holdingChanged();
    }

signals:
    void holdingChanged();

private:
    int m_id = -1;
    bool m_holdPending = false;
    bool m_holdCancelled = false;
};

#endif // DISPLAYKEEPER_H
