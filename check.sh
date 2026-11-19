#!/bin/bash
# check.sh — toutes les vérifications pre-commit en UNE commande
# (évite un appel IA : syntaxe + tests + confidentialité + i18n)
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./check.sh   — bash -n des scripts, node --check du src, npm test, grep brethous, grep chaines accentuees hors i18n"
  exit 0
fi
echo "== 1/5 bash -n des scripts"
for s in *.sh; do [ -f "$s" ] && bash -n "$s" && echo "  OK $s"; done
echo "== 2/5 node --check du src"
find src -name '*.mjs' -o -name '*.js' 2>/dev/null | while read -r f; do node --check "$f" || { echo "  ERREUR $f"; exit 1; }; done
echo "  OK"
echo "== 3/5 tests"
if [ -f package.json ] && grep -q '"test"' package.json; then npm test 2>&1 | tail -4; else
  for d in */; do [ -f "$d/package.json" ] && grep -q '"test"' "$d/package.json" && { (cd "$d" && npm test 2>&1 | tail -4); }; done
fi
echo "== 4/5 confidentialite (brethous)"
if grep -ri brethous --exclude-dir=.git --exclude-dir=node_modules --exclude-dir=build --exclude-dir=dist --exclude=AGENTS.md --exclude=check.sh --exclude=git-auto.sh --exclude=menu.sh . 2>/dev/null | grep -q .; then
  echo "  ERREUR: 'brethous' detecte — commit interdit"; exit 1
else echo "  OK aucun"; fi
echo "== 5/5 i18n (chaines accentuees hors catalogue)"
HITS=$(grep -rnP '[\x{00C0}-\x{00FF}]' --include='*.mjs' --include='*.js' --include='*.html' src/ 2>/dev/null | grep -vi 'i18n' | head -10 || true)
if [ -n "$HITS" ]; then echo "  A VERIFIER (hors catalogue i18n):"; echo "$HITS"; else echo "  OK aucun"; fi
echo "== check termine"
