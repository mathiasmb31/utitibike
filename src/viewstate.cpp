#include "appdata.h"
#include "viewstate.h"

#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QtMath>
#include <QDebug>

ViewState::ViewState(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() {
        const QString base = appDataDir();
        QDir().mkpath(base);
        QSaveFile f(base + QStringLiteral("/view.json"));
        if (!f.open(QIODevice::WriteOnly))
            return;
        QJsonObject o;
        o.insert(QStringLiteral("centerLat"), m_lat);
        o.insert(QStringLiteral("centerLon"), m_lon);
        o.insert(QStringLiteral("zoom"), m_zoom);
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    });
    load();
}

void ViewState::setCenterLat(double v)
{
    if (qFuzzyCompare(m_lat, v))
        return;
    m_lat = v;
    emit centerLatChanged();
    scheduleSave();
}

void ViewState::setCenterLon(double v)
{
    if (qFuzzyCompare(m_lon, v))
        return;
    m_lon = v;
    emit centerLonChanged();
    scheduleSave();
}

void ViewState::setZoom(int v)
{
    if (m_zoom == v)
        return;
    m_zoom = v;
    emit zoomChanged();
    scheduleSave();
}

void ViewState::scheduleSave()
{
    m_saveTimer.start();
}

void ViewState::load()
{
    const QString path = appDataDir()
                         + QStringLiteral("/view.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonValue lat = o.value(QStringLiteral("centerLat"));
    const QJsonValue lon = o.value(QStringLiteral("centerLon"));
    const QJsonValue zoom = o.value(QStringLiteral("zoom"));
    if (!lat.isDouble() || !lon.isDouble() || !zoom.isDouble())
        return;
    m_lat = lat.toDouble();
    m_lon = lon.toDouble();
    m_zoom = qBound(3, int(zoom.toDouble()), 17);
    m_restored = true;
    qWarning() << "viewstate restored" << m_lat << m_lon << "zoom" << m_zoom;
}
