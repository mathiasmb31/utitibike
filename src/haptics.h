#ifndef HAPTICS_H
#define HAPTICS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtDBus>

// Short vibration feedback through Lomiri's haptic daemon (hfd).
// Unlike the URL dispatcher, hfd runs as root on the *system* bus; the
// default click AppArmor profile allows exactly this call:
//   dbus (send) bus=system path=/com/lomiri/hfd
//               interface=com.lomiri.hfd.Vibrator peer=(label=unconfined)
class Haptics : public QObject
{
    Q_OBJECT
public:
    explicit Haptics(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void vibrate(int durationMs = 60)
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
                    QStringLiteral("com.lomiri.hfd"),
                    QStringLiteral("/com/lomiri/hfd"),
                    QStringLiteral("com.lomiri.hfd.Vibrator"),
                    QStringLiteral("vibrate"));
        msg.setArguments({ QVariant(durationMs) });
        // Fire and forget: hfd answers in a few ms and must never block the UI.
        QDBusConnection::systemBus().send(msg);
    }
};

#endif // HAPTICS_H
