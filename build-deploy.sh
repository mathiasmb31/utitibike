#!/bin/bash
# Build + install mathias.utitibike sur le téléphone (adb <serial>)
set -e

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./build-deploy.sh [jobs]"
  echo "  Lance build-fast.sh (CMake/QML) puis clickable build + install sur adb <serial>"
  echo "  Exemple: ./build-deploy.sh 4"
  exit 0
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
# v0.99.1: ${{...}} etait une mauvaise substitution bash (script mort).
JOBS="${1:-4}"
DEV="${ADB_DEVICE:-<serial>}"

echo "=== Build mathias.utitibike (jobs=$JOBS) ==="
./build-fast.sh "$JOBS" 2>&1 | tee /tmp/utitibike-build-fast.log | tail -10
# pipefail : sans cela le code de sortie etait celui de tail (0) et un
# echec de compilation etait masque.
if grep -qiE "error:|FAILED:" /tmp/utitibike-build-fast.log; then
  echo "ERREUR de build — abandon"; exit 1
fi

echo "=== Clickable build + install ==="
clickable build --arch arm64 2>&1 | tee /tmp/utitibike-clickable.log | tail -5
if ! grep -q "Successfully built package" /tmp/utitibike-clickable.log; then
  echo "ERREUR clickable build — abandon"; exit 1
fi
clickable install --serial-number "$DEV" 2>&1 | tail -3

echo "=== Vérification ==="
adb -s "$DEV" shell 'click info mathias.utitibike | grep version' 2>/dev/null || echo "adb non connecté"
echo "=== Fait ==="
