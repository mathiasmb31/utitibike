#!/bin/bash
# bump-version.sh <X.Y.Z> — synchronise la version dans les sources du projet.
# v0.99.1: dans utitibike, la source de verite est CMakeLists.txt:7
# (project(utitibike VERSION ...)) qui regenere manifest.json depuis
# manifest.json.in via configure_file. Il n'y a PAS de package.json ici.
set -e
if [ "$1" = "--help" ] || [ "$1" = "-h" ] || [ -z "$1" ]; then
  echo "Usage: ./bump-version.sh <X.Y.Z>"
  echo "  -> CMakeLists.txt (source de verite) + manifest.json"
  exit 0
fi
VER="$1"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if grep -q 'project(utitibike VERSION ' CMakeLists.txt; then
  sed -i -E "s/project\(utitibike VERSION [^ )]+/project(utitibike VERSION $VER/" CMakeLists.txt
  echo "bump: CMakeLists.txt -> $VER"
else
  echo "ERREUR: project(utitibike VERSION ...) introuvable dans CMakeLists.txt"
  exit 1
fi

# manifest.json est genere, mais on le synchronise aussi pour que le clickable
# build immediat (sans reconfigure) voie la bonne version.
if [ -f manifest.json ]; then
  sed -i -E "s/(\"version\"[[:space:]]*:[[:space:]]*\")[^\"]+\"/\1$VER\"/" manifest.json
  echo "bump: manifest.json -> $VER"
fi

echo "OK: version $VER"
