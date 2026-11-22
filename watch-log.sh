#!/bin/bash
# watch-log.sh — suivi live des logs de UT WheelTrek sur le device (device défini par ADB_DEVICE)
# v0.99.1: l'ancien script (copie d'utoutlook) cherchait un wrapper.log qui
# n'existe pas ici et un manifest.json en sous-dossier. Cette app C++ loggue
# via journalctl --user (qWarning depuis aa-exec / utitibike).
set -e
DEV="${DEVICE:-${ADB_DEVICE:-${ADB_SERIAL:-$(adb devices 2>/dev/null | awk 'NR==2 {print $1}')}}}"
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./watch-log.sh [filtre]"
  echo "  filtres: error|gps|record|tile|audio|all (defaut: all)"
  echo "  ex: ./watch-log.sh gps -> fixes, pause et reprise GPS"
  exit 0
fi
FILTER="${1:-all}"

if ! adb devices 2>/dev/null | grep -q "^${DEV}[[:space:]]*device"; then
  echo "ERREUR: device $DEV non connecté (adb devices)" >&2
  exit 1
fi
# Garde-fou : /home/phablet chiffré, illisible si le telephone est verrouille.
if ! adb -s "$DEV" shell 'test -d /home/phablet/.local/share/mathias.utitibike' 2>/dev/null; then
  echo "ERREUR: donnees de l'app illisibles sur $DEV — appareil verrouille apres reboot ?" >&2
  echo "  Déverrouillez le téléphone puis relancez." >&2
  exit 1
fi

BASE="journalctl --user -f --no-pager 2>/dev/null | grep -a -E 'utitibike|aa-exec'"
case "$FILTER" in
  error)   PAT='FATAL|error|failed|DENIED|crash|segfault' ;;
  gps)     PAT='\[gpsd\].*(fix|pause|resume|GPS recovery|position|error)|location service' ;;
  record)  PAT='\[gpsd\]|\[daemonctl\]|recording|parcours' ;;
  tile)    PAT='tilecache|tileUrl|tuile' ;;
  audio)   PAT='sound|paplay|beep|alarm' ;;
  *)       PAT='.' ;;
esac
echo "logs UT WheelTrek sur $DEV (filtre=$FILTER, Ctrl+C pour quitter)"
adb -s "$DEV" shell "${BASE} | grep -a -E '${PAT}'"
