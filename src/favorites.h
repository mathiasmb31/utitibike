#ifndef FAVORITES_H
#define FAVORITES_H

#include <QObject>
#include <QVariantList>

// Lieux favoris persistants (JSON). Chaque favori : nom, lat, lon, catégorie.
class Favorites : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)

public:
    explicit Favorites(QObject *parent = nullptr);

    QVariantList entries() const { return m_entries; }

    Q_INVOKABLE void add(const QString &name, double lat, double lon,
                         const QString &category = QString());
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    // Vérifie si un point est déjà en favori (à 10m près)
    Q_INVOKABLE bool isFavorite(double lat, double lon) const;

signals:
    void entriesChanged();

private:
    void load();
    void save();

    QVariantList m_entries;
};

#endif // FAVORITES_H
