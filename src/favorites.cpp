#include "favorites.h"
#include "geo.h"

#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QtMath>
#include <QDebug>

Favorites::Favorites(QObject *parent)
    : QObject(parent)
{
    load();
}

void Favorites::add(const QString &name, double lat, double lon, const QString &category)
{
    // Évite les doublons (à 10m près)
    if (isFavorite(lat, lon))
        return;
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("lat"), lat);
    entry.insert(QStringLiteral("lon"), lon);
    entry.insert(QStringLiteral("category"), category.isEmpty() ? QStringLiteral("autre") : category);
    m_entries.append(entry);
    save();
    emit entriesChanged();
}

void Favorites::remove(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    save();
    emit entriesChanged();
}

void Favorites::clear()
{
    m_entries.clear();
    save();
    emit entriesChanged();
}

bool Favorites::isFavorite(double lat, double lon) const
{
    for (const QVariant &v : m_entries) {
        const QVariantMap m = v.toMap();
        if (haversine(lat, lon,
                      m.value(QStringLiteral("lat")).toDouble(),
                      m.value(QStringLiteral("lon")).toDouble()) < 10.0)
            return true;
    }
    return false;
}

void Favorites::load()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/favorites.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return;
    for (const QJsonValue &v : doc.array()) {
        if (v.isObject())
            m_entries.append(v.toObject().toVariantMap());
    }
}

void Favorites::save()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/favorites.json");
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QJsonArray arr;
    for (const QVariant &v : m_entries)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}
