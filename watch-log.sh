#!/bin/bash
# watch-log.sh — tail live des logs de l'app sur le device adb (FP5 <serial>)
set -e
DEV="${DEVICE:-${ADB_SERIAL:-<serial>}}"
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./watch-log.sh [filtre]"
  echo "  filtres: error|loop|page|cookie|2fa|wrapper|all (defaut: all)"
  echo "  ex: ./watch-log.sh error   -> uniquement les erreurs"
  exit 0
fi
FILTER="${1:-all}"
LOG="/home/phablet/.config/$(grep -o '\"name\": *\"[^\"]*\"' */manifest.json 2>/dev/null | head -1 | sed 's/.*: *\"//;s/\"//')/wrapper.log"
[ -f /dev/null ] && LOG="${LOG:-/home/phablet/.config/APP/wrapper.log}"
# v4.1.0 (ref utoutlook): garde-fou — /home/phablet est chiffré sous UT ;
# après un reboot non déverrouillé le log est illisible (ENOKEY).
if ! adb devices 2>/dev/null | grep -q "^${DEV}[[:space:]]*device"; then
  echo "ERREUR: device $DEV non connecté (adb devices)" >&2
  exit 1
fi
if ! adb -s "$DEV" shell "test -f '$LOG'" 2>/dev/null; then
  echo "ERREUR: $LOG illisible sur le device $DEV — appareil verrouillé après reboot ?" >&2
  echo "  Déverrouillez le téléphone (PIN/mot de passe) puis relancez." >&2
  exit 1
fi
echo "log: $LOG sur $DEV via adb (Ctrl+C pour quitter)"
case "$FILTER" in
  error)  adb -s "$DEV" shell "tail -f '$LOG' | grep -a -i 'error\|fail\|431\|ERR_'" ;;
  loop)   adb -s "$DEV" shell "tail -f '$LOG' | grep -a -i 'loop\|stagnant\|relaunch\|post.srf'" ;;
  page)   adb -s "$DEV" shell "tail -f '$LOG' | grep -a 'Page loaded\|EVAL'" ;;
  cookie) adb -s "$DEV" shell "tail -f '$LOG' | grep -a -i 'cookie\|431\|token'" ;;
  2fa)    adb -s "$DEV" shell "tail -f '$LOG' | grep -a -i '2fa\|post.srf\|auth'" ;;
  wrapper) adb -s "$DEV" shell "tail -f '$LOG' | grep -a -v 'console\.'" ;;
  *)      adb -s "$DEV" shell "tail -f '$LOG'" ;;
esac
