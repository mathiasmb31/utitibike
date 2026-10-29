#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QObject>
#include <QScreen>
#include <QGuiApplication>
#include <QFile>
#include <QDir>
#include <QList>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDebug>

// Détection des « cutouts » d'écran (poinçon caméra, encoches, coins
// arrondis) pour que le contenu de l'app ne soit pas caché derrière.
//
// Source principale — générique, valable pour tout modèle : la config
// deviceinfo du système (/etc/deviceinfo/devices/*.yaml), la MÊME que celle
// que le shell Lomiri lit pour placer ses indicateurs hors des cutouts
// (DisplayCutoutsCollapsed : rectangles « x y w h » en pixels physiques
// portrait). Le modèle est identifié via /proc/device-tree/model (repli :
// PRETTY_HOSTNAME de /etc/machine-info).
//
// Repli : table intégrée indexée par modèle (poinçon centré en haut,
// profondeur mise à l'échelle de la dalle) — couvre les modèles connus si
// la config deviceinfo est absente. Sans aucune info : insets nuls, donc
// affichage inchangé (comportement antérieur).
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
    Q_PROPERTY(QString model READ model NOTIFY modelChanged)

public:
    explicit DeviceInfo(QObject *parent = nullptr)
        : QObject(parent)
    {
        detectModel();
        int pw = 0, ph = 0;
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QSize sz = screen->size();
            pw = qMin(sz.width(), sz.height());
            ph = qMax(sz.width(), sz.height());
            connect(screen, &QScreen::geometryChanged,
                    this, &DeviceInfo::recompute);
            connect(screen, &QScreen::orientationChanged,
                    this, &DeviceInfo::recompute);
        }
        m_cutouts = loadCutouts(pw, ph);
        recompute();
        log(QStringLiteral("model=%1 source=%2 screen=%3x%4 insets t/b/l/r=%5/%6/%7/%8")
                .arg(m_model, m_cutoutSource)
                .arg(m_screenW).arg(m_screenH)
                .arg(m_top).arg(m_bottom).arg(m_left).arg(m_right));
    }

    int safeTop() const { return m_top; }
    int safeBottom() const { return m_bottom; }
    int safeLeft() const { return m_left; }
    int safeRight() const { return m_right; }
    bool hasCutout() const
    {
        return m_top > 0 || m_bottom > 0 || m_left > 0 || m_right > 0;
    }
    QString model() const { return m_model; }

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
        const int w = sz.width();
        const int h = sz.height();
        // Les cutouts deviceinfo sont en pixels physiques PORTRAIT.
        const int pw = qMin(w, h);
        const int ph = qMax(w, h);
        const bool landscape = w > h;
        const int curW = landscape ? ph : pw;
        const int curH = landscape ? pw : ph;

        // Rotation 90° en paysage : (x,y,w,h) -> (y, pw-x-w, h, w).
        // Les profondeurs depuis les bords ne dépendant pas du sens de
        // rotation, une seule rotation suffit pour calculer les insets.
        auto rotate = [landscape, pw](const Rect &r) {
            return landscape ? Rect{ r.y, pw - r.x - r.w, r.h, r.w } : r;
        };

        // Profondeurs depuis les bords haut/bas.
        int top = 0, bottom = 0;
        for (const Rect &r : m_cutouts) {
            const Rect q = rotate(r);
            if (q.y <= 2)
                top = qMax(top, q.h);
            if (q.y + q.h >= curH - 2)
                bottom = qMax(bottom, q.h);
        }
        // Profondeurs depuis les bords gauche/droite, en ignorant les
        // rectangles entièrement dans les zones haute/basse (coins arrondis
        // — déjà dégagés par les insets top/bottom).
        int leftDepth = 0, rightDepth = 0;
        for (const Rect &r : m_cutouts) {
            const Rect q = rotate(r);
            if (q.y + q.h <= top + 2)
                continue;
            if (q.y >= curH - bottom - 2)
                continue;
            if (q.x <= 2)
                leftDepth = qMax(leftDepth, q.w);
            if (q.x + q.w >= curW - 2)
                rightDepth = qMax(rightDepth, q.w);
        }
        // En paysage, le poinçon peut être à gauche OU à droite selon le
        // sens de rotation : le côté le plus profond est appliqué des deux
        // côtés (conservateur). En portrait les deux sont nuls.
        const int side = qMax(leftDepth, rightDepth);

        if (top != m_top || bottom != m_bottom
                || side != m_left || side != m_right
                || w != m_screenW || h != m_screenH) {
            m_top = top;
            m_bottom = bottom;
            m_left = side;
            m_right = side;
            m_screenW = w;
            m_screenH = h;
            emit safeInsetsChanged();
        }
    }

signals:
    void safeInsetsChanged();
    void modelChanged();

private:
    struct Rect {
        int x, y, w, h;
    };

    static QString readFirstLine(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        QString line = QString::fromUtf8(f.readLine());
        f.close();
        line.remove(QLatin1Char('\0'));
        return line.trimmed();
    }

    // « fp4 » (device-tree), « Fairphone 4 » (PRETTY_HOSTNAME), etc.
    void detectModel()
    {
        m_model = readFirstLine(QStringLiteral("/proc/device-tree/model"));
        if (m_model.isEmpty())
            m_model = readFirstLine(
                QStringLiteral("/sys/firmware/devicetree/base/model"));
        if (m_model.isEmpty()) {
            QFile f(QStringLiteral("/etc/machine-info"));
            if (f.open(QIODevice::ReadOnly)) {
                while (!f.atEnd()) {
                    const QString line =
                        QString::fromUtf8(f.readLine()).trimmed();
                    if (line.startsWith(QStringLiteral("PRETTY_HOSTNAME="))) {
                        m_model = line.mid(15).trimmed();
                        if (m_model.size() >= 2
                                && m_model.startsWith(QLatin1Char('"'))
                                && m_model.endsWith(QLatin1Char('"')))
                            m_model = m_model.mid(1, m_model.size() - 2);
                        break;
                    }
                }
                f.close();
            }
        }
    }

    // « - 468 0 115 81 » -> [468, 0, 115, 81] (4 entiers, sinon liste vide).
    static QList<int> parseRect(const QString &item)
    {
        QList<int> v;
        const QStringList parts = item.split(
            QRegularExpression(QStringLiteral("\\s+")),
            QString::SkipEmptyParts);
        if (parts.size() != 4)
            return v;
        for (const QString &p : parts) {
            bool ok = false;
            const int n = p.toInt(&ok);
            if (!ok)
                return QList<int>();
            v.append(n);
        }
        return v;
    }

    // Lit les cutouts depuis la config deviceinfo du système — même source
    // que le shell Lomiri pour ses indicateurs. Rectangles en pixels portrait.
    QList<Rect> loadCutoutsFromDeviceInfo()
    {
        QList<Rect> result;
        const QString model = m_model.toLower();
        if (model.isEmpty())
            return result;
        QDir dir(QStringLiteral("/etc/deviceinfo/devices"));
        const QStringList files = dir.entryList(
            QStringList() << QStringLiteral("*.yaml"),
            QDir::Files, QDir::Name);
        for (const QString &fileName : files) {
            QFile f(dir.absoluteFilePath(fileName));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            // Un bloc = une entrée « Nom: » en colonne 0. Dans un bloc :
            //   Names:           -> liste de noms  («   - fp4 »)
            //   DisplayCutouts*: -> rectangles      («   - x y w h »)
            QString blockName;
            QString section;
            QStringList names;
            QList<Rect> collapsed, collapsedLight, expanded;
            auto closeBlock = [&]() {
                if (blockName.isEmpty() || !result.isEmpty())
                    return;
                bool match = blockName.toLower() == model;
                if (!match) {
                    for (const QString &n : names) {
                        const QString nl = n.toLower();
                        if (nl == model
                                || (nl.size() >= 3 && model.contains(nl))
                                || (model.size() >= 3 && nl.contains(model))) {
                            match = true;
                            break;
                        }
                    }
                }
                if (match) {
                    if (!collapsed.isEmpty())
                        result = collapsed;
                    else if (!collapsedLight.isEmpty())
                        result = collapsedLight;
                    else
                        result = expanded;
                    m_cutoutSource =
                        QStringLiteral("deviceinfo:") + fileName;
                }
            };
            while (!f.atEnd()) {
                const QString raw = QString::fromUtf8(f.readLine());
                const QString line = raw.trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                const bool indented = raw.startsWith(QLatin1Char(' '))
                                      || raw.startsWith(QLatin1Char('\t'));
                if (!indented && line.endsWith(QLatin1Char(':'))) {
                    closeBlock();
                    blockName = line.left(line.size() - 1).trimmed();
                    section.clear();
                    names.clear();
                    collapsed.clear();
                    collapsedLight.clear();
                    expanded.clear();
                } else if (line.startsWith(QLatin1Char('-'))) {
                    const QString item = line.mid(1).trimmed();
                    if (section == QLatin1String("Names")) {
                        names.append(item);
                    } else {
                        const QList<int> v = parseRect(item);
                        if (v.size() == 4) {
                            const Rect rect{ v[0], v[1], v[2], v[3] };
                            if (section == QLatin1String("DisplayCutoutsCollapsed"))
                                collapsed.append(rect);
                            else if (section == QLatin1String("DisplayCutoutsCollapsedLight"))
                                collapsedLight.append(rect);
                            else if (section == QLatin1String("DisplayCutoutsExpanded"))
                                expanded.append(rect);
                        }
                    }
                } else if (indented && line.endsWith(QLatin1Char(':'))) {
                    section = line.left(line.size() - 1).trimmed();
                }
            }
            closeBlock();
            f.close();
            if (!result.isEmpty())
                break;  // config trouvée pour ce modèle
        }
        return result;
    }

    // Config deviceinfo si disponible, sinon table intégrée par modèle.
    QList<Rect> loadCutouts(int pw, int ph)
    {
        QList<Rect> rects = loadCutoutsFromDeviceInfo();
        if (!rects.isEmpty())
            return rects;
        // Repli : poinçon centré en haut, profondeur mise à l'échelle de la
        // dalle. Fairphone 4 : 115x81 px sur dalle 1080x2340 (valeurs de la
        // config deviceinfo FP4 du port UBports).
        const QString model = m_model.toLower();
        int holeW = 0, holeH = 0;
        const int refW = 1080, refH = 2340;
        if (model.contains(QStringLiteral("fp4"))
                || model.contains(QStringLiteral("fairphone 4"))) {
            holeW = 115;
            holeH = 81;
        }
        if (holeH > 0 && pw > 0 && ph > 0) {
            m_cutoutSource = QStringLiteral("builtin");
            const int w = qRound(holeW * static_cast<double>(pw) / refW);
            const int h = qRound(holeH * static_cast<double>(ph) / refH);
            rects.append(Rect{ (pw - w) / 2, 0, w, h });
            return rects;
        }
        m_cutoutSource = QStringLiteral("none");
        return rects;
    }

    // Trace qDebug + fichier dans le cache (couvert par l'AppArmor du click)
    // pour vérifier sur le téléphone ce que l'app a détecté.
    void log(const QString &msg)
    {
        qDebug() << "DeviceInfo:" << msg;
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        QFile f(dir + QStringLiteral("/deviceinfo.log"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            f.write((msg + QLatin1Char('\n')).toUtf8());
            f.close();
        }
    }

    QString m_model;
    QString m_cutoutSource;
    QList<Rect> m_cutouts;
    int m_top = 0;
    int m_bottom = 0;
    int m_left = 0;
    int m_right = 0;
    int m_screenW = 0;
    int m_screenH = 0;
};

#endif // DEVICEINFO_H
