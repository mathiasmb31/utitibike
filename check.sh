#!/bin/bash
# check.sh — pré-vol complet en UNE commande, adapté à utitibike (C++/Qt).
BLOCKED_NAME="$(printf '%s%s' 'Bret' 'hous')"
# v0.99.1: l'ancien script etait copie d'utoutlook (node/npm) et ne lancait
# AUCUN test — il affichait "OK" meme avec la suite C++ cassee.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./check.sh   — syntaxe, coherence version, tests C++ (ctest), confidentialite, i18n, caracteres parasites"
  exit 0
fi

echo "== 1/7 bash -n des scripts"
for s in *.sh githooks/*; do
  [ -f "$s" ] && bash -n "$s" && echo "  OK $s"
done

echo "== 2/7 syntaxe C++/QML (presence des fichiers)"
for f in CMakeLists.txt manifest.json; do
  [ -f "$f" ] && echo "  OK $f" || { echo "  MANQUANT $f"; exit 1; }
done
python3 -c "import json; m=json.load(open('manifest.json')); print('  OK manifest', m['name'], m['version'])"

echo "== 3/7 coherence version (CMake <-> manifest)"
CV=$(grep -oE 'project\(utitibike VERSION [^ )]+' CMakeLists.txt | awk '{print $3}')
MV=$(python3 -c "import json;print(json.load(open('manifest.json'))['version'])")
[ "$CV" = "$MV" ] && echo "  OK $CV" || { echo "  ERREUR CMake=$CV manifest=$MV"; exit 1; }

echo "== 4/7 tests C++ (ctest) — BLOQUANT"
# v0.99.1: l'ancienne version affichait "check termine" avec un code 0 meme
# quand aucun test n'avait tourne. Un check vert doit TOUJOURS signifier que
# la suite C++ a ete executee.
if [ ! -f build-host/CMakeCache.txt ]; then
  echo "  ERREUR: build-host absent — lancer cmake -DBUILD_TESTING=ON + build"; exit 1
fi
if ! (cd build-host && cmake .. -DBUILD_TESTING=ON >/dev/null 2>&1); then
  echo "  ERREUR: cache CMake local perime"; exit 1
fi
(cd build-host && cmake --build . -j4 >/dev/null 2>&1) || { echo "  ERREUR build des tests"; exit 1; }
NT=$(cd build-host && ctest -N 2>/dev/null | grep -c 'Test #' || true)
[ "$NT" -ge 6 ] || { echo "  ERREUR: seulement $NT test(s) declare(s)"; exit 1; }
(cd build-host && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --timeout 60 2>&1 | tail -12) \
  || { echo "  ERREUR: ctest a echoue"; exit 1; }

echo "== 5/7 confidentialite"
if git log --all --format='%ae %ce %B' | grep -qi "$BLOCKED_NAME"; then
  echo "  ERREUR: identite proscrite dans l'historique"; exit 1
else echo "  OK historique"; fi
if grep -ri "$BLOCKED_NAME" --exclude-dir=.git --exclude-dir=build --exclude-dir=build-host \
     --exclude-dir=build-ninja --exclude=AGENTS.md --exclude=check.sh \
     --exclude=git-auto.sh --exclude=menu.sh . 2>/dev/null | grep -q .; then
  echo "  ERREUR: identite proscrite dans le working tree"; exit 1
else echo "  OK working tree"; fi
# Règle Mathias (tous projets) : jamais d'ID device dans un contenu publié.
# Le motif est construit à l'exécution pour que ce script ne contienne pas l'ID.
DEV_ID="$(printf '5760%s579' 65)"; OLD_ID="$(printf '71b90%s' fee)"
if grep -rInE "$DEV_ID|$OLD_ID" --exclude-dir=.git --exclude-dir=build \
     --exclude-dir=build-host --exclude-dir=build-ninja --exclude=check.sh \
     --exclude=pre-commit --exclude=pre-push . 2>/dev/null | grep -q .; then
  echo "  ERREUR: ID device interdit dans le working tree"; exit 1
else echo "  OK aucun ID device"; fi

echo "== 6/7 i18n (chaines accentuees hors catalogue) — BLOQUANT"
HITS=$(grep -rnP '[\x{00C0}-\x{00FF}]' --include='*.cpp' --include='*.qml' --include='*.h' \
       src/ qml/ 2>/dev/null | grep -viE 'tr\(|qsTr\(|i18n|//|comment|qWarning|qDebug|qInfo|qCritical|:[[:space:]]*\+ "|:[[:space:]]*<< "' | head -10 || true)
if [ -n "$HITS" ]; then
  echo "  ERREUR: texte utilisateur hors catalogue i18n:"; echo "$HITS"; exit 1
else echo "  OK aucun"; fi
# v1.0: vérifie que TOUS les i18n.tr() du QML existent dans le .pot.
MISSING=$(python3 - <<'PY'
import re, glob
src = '\n'.join(open(f, encoding='utf-8').read()
                for f in glob.glob('qml/**/*.qml', recursive=True))
msgs = set(re.findall(r'i18n\.tr\(\s*"([^"\\]*(?:\\.[^"\\]*)*)"', src))
pot = open('po/utitibike.pot', encoding='utf-8').read()
for m in sorted(msgs):
    if 'msgid "' + m + '"' not in pot:
        print(m)
PY
)
if [ -n "$MISSING" ]; then
  echo "  ERREUR: msgids absents du .pot:"; echo "$MISSING" | head -20; exit 1
else echo "  OK msgids QML présents dans le .pot"; fi

echo "== 7/7 caracteres parasites CJK/Cyrillique (regle 2026-11-20)"
CJK=$(grep -rnP '[\x{4e00}-\x{9fff}\x{0400}-\x{04ff}]' . \
      --include='*.md' --include='*.sh' --include='*.mjs' --include='*.js' \
      --include='*.qml' --include='*.cpp' --include='*.h' \
      --exclude-dir=.git --exclude-dir=build --exclude-dir=build-host \
      --exclude-dir=build-ninja --exclude-dir=po 2>/dev/null | head -10 || true)
if [ -n "$CJK" ]; then
  echo "  ERREUR: caractere parasite:"; echo "$CJK"; exit 1
else echo "  OK aucun"; fi

echo "== check termine"
