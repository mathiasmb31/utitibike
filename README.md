# Utitibike

[![CI](https://github.com/mathiasmb31/utitibike/actions/workflows/ci.yml/badge.svg)](https://github.com/mathiasmb31/utitibike/actions/workflows/ci.yml)

Itinéraires vélo pour Ubuntu Touch : carte OpenStreetMap (tuiles OSM, CyclOSM,
Esri Dark Gray, satellite Esri), calcul d'itinéraire BRouter, enregistrement GPS
avec daemon détaché (survit à la fermeture de l'app), profil d'altitude et de
vitesse, suggestions de tracés Overpass, export/import GPX, favoris,
historique, recherche Nominatim.

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

Nécessite : Qt 5.12+ (Core, Gui, Quick, Network, DBus, Positioning, Multimedia), CMake,
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
  gpstracker.*      position GPS (QGeoPositionInfoSource, lazy au 1er start,
                    warmUp() au lancement : dernière position sans allumer le GPS)
  daemoncontroller.* pilotage du daemon détaché (gpsd.pid), polling recording.done,
                    reprise après crash, import GPX dans l'historique
  gpsdaemon/        daemon détaché (utitibike-gpsd) : points, distance, D+, watchdog
                    60 s, arrêt 1 h sans fix/sans mouvement, fichiers .part
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
po/                 catalogues gettext (QML) + Qt (C++), 14 langues
```

## Internationalisation

14 langues : en, fr, es, de, it, pt, nl, pl, sv, da, zh, ar, ca, eu
(les 10 de la convention transverse + nl/pl/sv/da hérités du projet). Les 4
nouveaux catalogues (zh/ar/ca/eu) démarrent avec un fallback anglais, à traduire.

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

Le profil AppArmor ne contient **plus** de `read_path`/`write_path` (supprimés
en 0.1.15 au profit de l'heuristique QScreen) : la clickable review passe sans
redflag de sécurité. Le groupe `audio` est présent pour les bips du daemon.

## Bugs

Tout bug doit être déclaré sur l'URL GitHub du projet :
**https://github.com/mathiasmb31/utitibike/issues**
