#include <QtTest>
#include "geo.h"

// Tests de la fonction haversine (distance orthodromique en mètres).
// géo.h est header-only : pas besoin de linker de source externe.
class TestGeo : public QObject
{
    Q_OBJECT
private slots:
    void samePointIsZero();
    void parisToulouse();
    void antipodes();
    void negativeCoordinates();
    void symmetry();
    void knownDistance1Degree();
};

void TestGeo::samePointIsZero()
{
    QCOMPARE(haversine(43.6044, 1.4442, 43.6044, 1.4442), 0.0);
}

void TestGeo::parisToulouse()
{
    // Paris (48.8566, 2.3522) → Toulouse (43.6044, 1.4442) ≈ 590 km
    const double d = haversine(48.8566, 2.3522, 43.6044, 1.4442);
    QVERIFY2(d > 550000.0 && d < 620000.0,
             qPrintable(QString("Distance Paris-Toulouse: %1 m (attendu ~590 km)").arg(d)));
}

void TestGeo::antipodes()
{
    // Antipodes : (0,0) → (0,180) ≈ moitié de la circonférence terrestre ≈ 20 015 km
    const double d = haversine(0.0, 0.0, 0.0, 180.0);
    QVERIFY2(d > 19000000.0 && d < 21000000.0,
             qPrintable(QString("Distance antipodes: %1 m (attendu ~20 015 km)").arg(d)));
}

void TestGeo::negativeCoordinates()
{
    // New York (-74.006, 40.7128) → Sydney (151.2093, -33.8688) ≈ 15 990 km
    const double d = haversine(40.7128, -74.006, -33.8688, 151.2093);
    QVERIFY2(d > 15000000.0 && d < 17000000.0,
             qPrintable(QString("Distance NY-Sydney: %1 m (attendu ~16 000 km)").arg(d)));
}

void TestGeo::symmetry()
{
    const double d1 = haversine(48.8566, 2.3522, 43.6044, 1.4442);
    const double d2 = haversine(43.6044, 1.4442, 48.8566, 2.3522);
    QCOMPARE(d1, d2);
}

void TestGeo::knownDistance1Degree()
{
    // 1 degré de latitude ≈ 111 km (constante sur toute la sphère)
    const double d = haversine(0.0, 0.0, 1.0, 0.0);
    QVERIFY2(d > 110000.0 && d < 112000.0,
             qPrintable(QString("1 degré lat: %1 m (attendu ~111 km)").arg(d)));
}

QTEST_GUILESS_MAIN(TestGeo)
#include "tst_geo.moc"
