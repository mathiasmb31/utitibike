#ifndef PALETTEWATCHER_H
#define PALETTEWATCHER_H

#include <QObject>
#include <QGuiApplication>
#include <QPalette>

// Exposé au QML (context property "paletteWatcher") : reflète le thème
// système de Lomiri. Le QPA Ubuntu Touch propage la palette de l'OS via
// QGuiApplication::paletteChanged quand l'utilisateur change
// Paramètres → Apparence (sombre/clair).
class PaletteWatcher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
public:
    explicit PaletteWatcher(QObject *parent = nullptr) : QObject(parent)
    {
        connect(qApp, &QGuiApplication::paletteChanged,
                this, &PaletteWatcher::darkChanged);
    }
    // Sombre si la couleur de fond système a une luminosité < 50 %
    bool dark() const
    {
        // lightnessF() n'existe qu'en Qt 5.14+ ; lightness() (0-255) suffit
        // et garde la compatibilité avec le minimum déclaré (Qt 5.12).
        return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    }
signals:
    void darkChanged();
};

#endif // PALETTEWATCHER_H
