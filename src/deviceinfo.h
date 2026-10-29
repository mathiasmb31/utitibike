#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QObject>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

// Détection des « cutouts » d'écran (poinçon caméra) pour que le contenu
// de l'app ne soit pas caché derrière.
//
// HEURISTIQUE ÉCRAN — aucun read_path AppArmor (sinon redflag et review
// manuel OpenStore) : les écrans avec poinçon ont un ratio hauteur/largeur
// ≥ 2,0 (19:9 et plus). Le poinçon est estimé centré en haut, sa profondeur
// mise à l'échelle de la dalle (référence Fairphone 4 : 81 px sur 2340 px
// de hauteur portrait, soit ~3,5 %).
//
// En paysage, le poinçon est sur un côté — la profondeur est appliquée des
// deux côtés (conservateur). Sans écran détecté ou ratio < 2,0 : insets
// nuls, affichage inchangé.
//
// Les insets sont exposés au QML (context property « deviceInfo ») qui les
// applique comme marges du PageStack : les pages démarrent sous la zone
// coupée, comme le shell le fait pour ses indicateurs.
class DeviceInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int safeTop READ safeTop NOTIFY safeInsetsChanged)
    Q_PROPERTY(int safeBottom READ safeBottom NOTIFY safeInsetsChanged)
    Q_PROPERTY(int safeLeft READ safeLeft NOTIFY safeInsetsChanged)
    Q_PROPERTY(int safeRight READ safeRight NOTIFY safeInsetsChanged)
    Q_PROPERTY(bool hasCutout READ hasCutout NOTIFY safeInsetsChanged)

public:
    explicit DeviceInfo(QObject *parent = nullptr)
        : QObject(parent)
    {
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            connect(screen, &QScreen::geometryChanged,
                    this, &DeviceInfo::recompute);
            connect(screen, &QScreen::orientationChanged,
                    this, &DeviceInfo::recompute);
        }
        recompute();
    }

    int safeTop() const { return m_top; }
    int safeBottom() const { return m_bottom; }
    int safeLeft() const { return m_left; }
    int safeRight() const { return m_right; }
    bool hasCutout() const
    {
        return m_top > 0 || m_bottom > 0 || m_left > 0 || m_right > 0;
    }

public slots:
    // Recalculé à chaque changement de géométrie/orientation de l'écran.
    void recompute()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QSize sz = screen->size();
        if (sz.isEmpty())
            return;

        const int pw = qMin(sz.width(), sz.height());
        const int ph = qMax(sz.width(), sz.height());
        const bool landscape = sz.width() > sz.height();

        // Ratio portrait hauteur/largeur. Seuil 2,0 pour couvrir 19:9 (2,111)
        // et les ratios plus hauts (19,5:9, 20:9, 21:9…).
        const double ratio = static_cast<double>(ph) / pw;
        int depth = 0;
        if (ratio >= 2.0) {
            // Poinçon centré. Référence FP4 : 81 px de profondeur sur
            // 2340 px de hauteur portrait → ~3,5 %.
            depth = qRound(ph * 0.035);
        }

        int top = 0, bottom = 0, left = 0, right = 0;
        if (depth > 0) {
            if (landscape) {
                // Poinçon sur un côté en paysage — appliqué des deux côtés
                // (conservateur, comme l'ancien code deviceinfo).
                left = depth;
                right = depth;
            } else {
                top = depth;
            }
        }

        if (top != m_top || bottom != m_bottom
                || left != m_left || right != m_right
                || sz.width() != m_screenW || sz.height() != m_screenH) {
            m_top = top;
            m_bottom = bottom;
            m_left = left;
            m_right = right;
            m_screenW = sz.width();
            m_screenH = sz.height();
            emit safeInsetsChanged();
        }

        qDebug() << "DeviceInfo: screen" << sz.width() << "x" << sz.height()
                 << "ratio" << QString::number(ratio, 'f', 2)
                 << "insets t/b/l/r ="
                 << m_top << "/" << m_bottom << "/" << m_left << "/" << m_right;
    }

signals:
    void safeInsetsChanged();

private:
    int m_top = 0;
    int m_bottom = 0;
    int m_left = 0;
    int m_right = 0;
    int m_screenW = 0;
    int m_screenH = 0;
};

#endif // DEVICEINFO_H
