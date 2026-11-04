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
        return QGuiApplication::palette().color(QPalette::Window).lightnessF() < 0.5;
    }
signals:
    void darkChanged();
};

#endif // PALETTEWATCHER_H
