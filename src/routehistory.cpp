#include "appdata.h"
#include "routehistory.h"

#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <QDebug>

RouteHistory::RouteHistory(QObject *parent)
    : QObject(parent)
{
    load();
}

QString RouteHistory::writeGpxFile(const QString &gpx)
{
    const QString dir = appDataDir()
                        + QStringLiteral("/history");
    QDir().mkpath(dir);
    const QString fileName = QUuid::createUuid().toString(QUuid::WithoutBraces)
                             + QStringLiteral(".gpx");
    QSaveFile f(dir + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(gpx.toUtf8());
    if (!f.commit()) {
        qWarning() << "RouteHistory gpx save FAILED" << f.fileName();
        return QString();
    }
    return fileName;
}

QString RouteHistory::gpxFilePath(const QString &fileName) const
{
    return appDataDir()
           + QStringLiteral("/history/") + fileName;
}

void RouteHistory::addEntry(const QString &name, double distanceM, double ascentM,
                            const QVariantList &waypoints, const QString &gpx)
{
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), name.isEmpty()
                 ? tr("Itinéraire %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd/MM HH:mm")))
                 : name);
    entry.insert(QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate));
    entry.insert(QStringLiteral("distanceM"), distanceM);
    entry.insert(QStringLiteral("ascentM"), ascentM);
    entry.insert(QStringLiteral("waypoints"), waypoints);
    // Le GPX part dans un fichier séparé : history.json ne garde que le nom.
    if (!gpx.isEmpty()) {
        const QString fileName = writeGpxFile(gpx);
        if (!fileName.isEmpty())
            entry.insert(QStringLiteral("gpxFile"), fileName);
    }

    // Limite à 50 entrées (les fichiers GPX orphelins sont supprimés)
    while (m_entries.size() >= 50) {
        const QString oldFile = m_entries.first().toMap()
                                .value(QStringLiteral("gpxFile")).toString();
        if (!oldFile.isEmpty())
            QFile::remove(gpxFilePath(oldFile));
        m_entries.removeFirst();
    }
    m_entries.prepend(entry);
    save();
    emit entriesChanged();
}

void RouteHistory::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    const QString fileName = m_entries.at(index).toMap()
                             .value(QStringLiteral("gpxFile")).toString();
    if (!fileName.isEmpty())
        QFile::remove(gpxFilePath(fileName));
    m_entries.removeAt(index);
    save();
    emit entriesChanged();
}

void RouteHistory::clear()
{
    for (const QVariant &v : m_entries) {
        const QString fileName = v.toMap().value(QStringLiteral("gpxFile")).toString();
        if (!fileName.isEmpty())
            QFile::remove(gpxFilePath(fileName));
    }
    m_entries.clear();
    save();
    emit entriesChanged();
}

QString RouteHistory::gpxAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QString();
    const QVariantMap e = m_entries.at(index).toMap();
    const QString fileName = e.value(QStringLiteral("gpxFile")).toString();
    if (!fileName.isEmpty()) {
        QFile f(gpxFilePath(fileName));
        if (f.open(QIODevice::ReadOnly))
            return QString::fromUtf8(f.readAll());
        return QString();
    }
    // Ancien format (GPX embarqué dans le JSON)
    return e.value(QStringLiteral("gpx")).toString();
}

QVariantList RouteHistory::waypointsAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QVariantList();
    return m_entries.at(index).toMap().value(QStringLiteral("waypoints")).toList();
}

void RouteHistory::load()
{
    const QString path = appDataDir()
                         + QStringLiteral("/history.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return;
    bool migrated = false;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        QVariantMap e = v.toObject().toVariantMap();
        // Migration : les anciennes versions embarquaient le GPX texte dans
        // le JSON ; on l'externalise dans un fichier séparé.
        const QString inlineGpx = e.value(QStringLiteral("gpx")).toString();
        if (!inlineGpx.isEmpty() && !e.contains(QStringLiteral("gpxFile"))) {
            const QString fileName = writeGpxFile(inlineGpx);
            if (!fileName.isEmpty()) {
                e.remove(QStringLiteral("gpx"));
                e.insert(QStringLiteral("gpxFile"), fileName);
                migrated = true;
            }
        }
        m_entries.append(e);
    }
    if (migrated)
        save();
}

void RouteHistory::save()
{
    const QString path = appDataDir()
                         + QStringLiteral("/history.json");
    QDir().mkpath(appDataDir());
    QJsonArray arr;
    for (const QVariant &v : m_entries)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}
