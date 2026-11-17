#!/bin/bash
# bump-version.sh <X.Y.Z> — synchronise la version dans manifest.json + package.json
# (règle Mathias : version synchronisée dans les deux AVANT le build)
set -e
if [ "$1" = "--help" ] || [ "$1" = "-h" ] || [ -z "$1" ]; then
  echo "Usage: ./bump-version.sh <X.Y.Z>"
  echo "  -> met à jour le champ \"version\" de tous les manifest.json / package.json du projet"
  exit 0
fi
VER="$1"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
N=0
for f in $(find . -maxdepth 4 \( -name manifest.json -o -name package.json \) \
             -not -path '*/node_modules/*' -not -path '*/build/*' -not -path '*/dist/*' 2>/dev/null); do
  if grep -q '"version"' "$f"; then
    sed -i -E "s/(\"version\"[[:space:]]*:[[:space:]]*\")[^\"]+\"/\1$VER\"/" "$f"
    echo "bump: $f -> $VER"
    N=$((N+1))
  fi
done
[ "$N" -gt 0 ] && echo "OK: $N fichier(s) -> version $VER" || { echo "AUCUN fichier de version trouvé"; exit 1; }
