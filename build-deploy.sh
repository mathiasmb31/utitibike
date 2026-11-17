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
JOBS="${{1:-4}}"

echo "=== Build mathias.utitibike (jobs=$JOBS) ==="
./build-fast.sh "$JOBS" 2>&1 | tail -10

echo "=== Clickable build + install ==="
clickable build --arch arm64 2>&1 | tail -5 || clickable build 2>&1 | tail -5
clickable install 2>&1 | tail -3

echo "=== Vérification ==="
adb -s <serial> shell 'click info mathias.utitibike | grep version' 2>/dev/null || echo "adb non connecté"
echo "=== Fait ==="
