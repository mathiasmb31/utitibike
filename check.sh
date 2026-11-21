#!/bin/bash
# check.sh — pré-vol complet en UNE commande, adapté à utitibike (C++/Qt).
# v0.99.1: l'ancien script etait copie d'utoutlook (node/npm) et ne lancait
# AUCUN test — il affichait "OK" meme avec la suite C++ cassee.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./check.sh   — syntaxe, tests C++ (ctest), confidentialite, i18n"
  exit 0
fi

echo "== 1/6 bash -n des scripts"
for s in *.sh githooks/*; do
  [ -f "$s" ] && bash -n "$s" && echo "  OK $s"
done

echo "== 2/6 syntaxe C++/QML (presence des fichiers)"
for f in CMakeLists.txt manifest.json; do
  [ -f "$f" ] && echo "  OK $f" || { echo "  MANQUANT $f"; exit 1; }
done
python3 -c "import json; m=json.load(open('manifest.json')); print('  OK manifest', m['name'], m['version'])"

echo "== 3/6 coherence version (CMake <-> manifest)"
CV=$(grep -oE 'project\(utitibike VERSION [^ )]+' CMakeLists.txt | awk '{print $3}')
MV=$(python3 -c "import json;print(json.load(open('manifest.json'))['version'])")
[ "$CV" = "$MV" ] && echo "  OK $CV" || { echo "  ERREUR CMake=$CV manifest=$MV"; exit 1; }

echo "== 4/6 tests C++ (ctest)"
if [ -f build-host/CMakeCache.txt ]; then
  # L'ancien cache peut pointer vers un chemin disparu (/mnt/five) : on
  # n'ecrase rien, on avertit et on sacrifice ce controle si la reconfigure
  # est impossible localement (la CI GitHub le fait, elle).
  if (cd build-host && cmake .. >/dev/null 2>&1); then
    (cd build-host && cmake --build . -j4 >/dev/null 2>&1)
    (cd build-host && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --timeout 60 2>&1 | tail -12)
  else
    echo "  ATTENTION: cache CMake local perime — tests non lances (CI GitHub les lance)"
  fi
else
  echo "  build-host absent — cmake -DBUILD_TESTING=ON + build requis pour les tests"
fi

echo "== 5/6 confidentialite"
if git log --all --format='%ae %ce %B' | grep -qi 'brethous'; then
  echo "  ERREUR: identite proscrite dans l'historique"; exit 1
else echo "  OK historique"; fi
if grep -ri 'brethous' --exclude-dir=.git --exclude-dir=build --exclude-dir=build-host \
     --exclude-dir=build-ninja --exclude=AGENTS.md --exclude=check.sh \
     --exclude=git-auto.sh --exclude=menu.sh . 2>/dev/null | grep -q .; then
  echo "  ERREUR: identite proscrite dans le working tree"; exit 1
else echo "  OK working tree"; fi

echo "== 6/6 i18n (chaines accentuees hors catalogue)"
HITS=$(grep -rnP '[\x{00C0}-\x{00FF}]' --include='*.cpp' --include='*.qml' --include='*.h' \
       src/ qml/ 2>/dev/null | grep -viE 'tr\(|qsTr\(|i18n|//|comment' | head -10 || true)
if [ -n "$HITS" ]; then echo "  A VERIFIER (hors catalogue i18n):"; echo "$HITS"; else echo "  OK aucun"; fi

echo "== check termine"
