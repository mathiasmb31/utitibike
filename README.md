# Utitibike

[![CI](https://github.com/mathiasmb31/utitibike/actions/workflows/ci.yml/badge.svg)](https://github.com/mathiasmb31/utitibike/actions/workflows/ci.yml)

Itinéraires vélo pour Ubuntu Touch : carte OpenStreetMap (tuiles OSM, CyclOSM,
CARTO sombre, satellite Esri), calcul d'itinéraire BRouter, enregistrement GPS,
profil d'altitude, export/import GPX, favoris, historique, recherche Nominatim.

## Tailles d'écran

L'interface s'adapte à toutes les tailles d'écran de téléphone : layout 100 % fluide
(ancres + unités relatives, aucun pixel fixe dans les pages), safe areas du poinçon
détectées et mises à l'échelle automatiquement (ratio hauteur/largeur ≥ 2,0, profondeur
proportionnelle à la hauteur de l'écran, portrait et paysage gérés), carte plein écran
avec zoom/pan.

## Build & install

```sh
clickable build
clickable install build/aarch64-linux-gnu/app/mathias.utitibike_<version>_arm64.click
```

Nécessite : Qt 5.12+ (Core, Gui, Quick, Network, DBus, Positioning), CMake,
gettext (msgfmt), qt5 lrelease, python3.

## Architecture

```
src/
  main.cpp          démarrage, context properties, i18n (QTranslator)
  tilecache.*       tuiles : cache mémoire (100) + disque (quota 200 Mo, purge LRU),
                    téléchargements (6 parallèles, timeout 15 s, redirect suivi),
                    auto-détection réseau (online)
  tilemapitem.*     rendu carte (QQuickPaintedItem) : tuiles, itinéraire, tracé GPS,
                    waypoints, favoris, position, attribution, culling des tracés
  brouterclient.*   calcul d'itinéraire BRouter (HTTP), navigation turn-by-turn,
                    export GPX (horodatage ajouté), import GPX
  gpstracker.*      position GPS (QGeoPositionInfoSource, lazy au 1er start)
  routerecorder.*   enregistrement GPS : points, distance, D+, auto-save 60 s
  routehistory.*    historique des itinéraires (JSON + GPX externalisés)
  favorites.*       favoris (JSON)
  viewstate.*       dernière vue de la carte persistée (view.json)
  geo.h             haversine (implémentation unique, C++)
  geoutils.*        singleton QML GeoUtils (haversine pour le QML)
  urlopener.h       ouverture d'URL via le dispatcher Lomiri (D-Bus async)
  displaykeeper.h   garde l'écran allumé (repowerd, keep-display-on)
  appstate.h        état premier/arrière-plan
  haptics.h         vibrations
qml/
  Main.qml          MainView + domaine i18n (gettext)
  MapPage.qml       carte + panneau itinéraire + recherche + enregistrement
                    + favoris (panneau latéral)
  HistoryPage.qml   historique
  AboutPage.qml     à propos
  ExportPage.qml    export GPX via le Content Hub
  ElevationProfile.qml  profil d'altitude (Canvas)
po/                 catalogues gettext (QML) + Qt (C++), 10 langues
```

## Internationalisation

10 langues : en, fr, es, de, it, pt, nl, pl, sv, da.

- QML (`i18n.tr`) = gettext/libintl : `po/utitibike_<lang>.po` → `.mo` installé
  dans `share/locale/<lang>/LC_MESSAGES/utitibike.mo` (domaine posé dans
  `qml/Main.qml`).
- C++ (`tr()`) = QTranslator/Qt : `po/utitibike-cpp_<lang>.po` → `.ts` (via
  `po/po2ts.py`) → `.qm` à la racine du click (chargé dans `src/main.cpp`).

## Version

Unique source de vérité : `project(utitibike VERSION x.y.z ...)` dans
`CMakeLists.txt`. Sont générés/déduits : `manifest.json` (via
`manifest.json.in`), la macro C++ `UTITIBIKE_VERSION` et le context property
QML `appVersion`.

## OpenStore (click review)

Le review remonte des redflags AppArmor `read_path`/`write_path` : ce sont les
chemins de données de l'app (`~/.cache|config|local/share/utitibike/**`),
écrits via QSaveFile (écriture atomique). Ils sont nécessaires au cache de
tuiles, à l'historique, aux favoris et à la vue persistée — à documenter dans
la description de l'OpenStore.

## Bugs

Tout bug doit être déclaré sur l'URL GitHub du projet :
**https://github.com/mathiasmb31/utitibike/issues**
