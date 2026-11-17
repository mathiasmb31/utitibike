#!/bin/bash
# watch-log.sh — tail live des logs de l'app sur le device adb (FP5 <serial>)
set -e
ADB_SERIAL="${ADB_SERIAL:-<serial>}"
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./watch-log.sh [filtre]"
  echo "  filtres: error|loop|page|cookie|2fa|wrapper|all (defaut: all)"
  echo "  ex: ./watch-log.sh error   -> uniquement les erreurs"
  exit 0
fi
FILTER="${1:-all}"
LOG="/home/phablet/.config/$(grep -o '\"name\": *\"[^\"]*\"' */manifest.json 2>/dev/null | head -1 | sed 's/.*: *\"//;s/\"//')/wrapper.log"
[ -f /dev/null ] && LOG="${LOG:-/home/phablet/.config/APP/wrapper.log}"
echo "log: $LOG (adb -s $ADB_SERIAL) — Ctrl+C pour quitter"
case "$FILTER" in
  error)  adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a -i 'error\|fail\|431\|ERR_'" ;;
  loop)   adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a -i 'loop\|stagnant\|relaunch\|post.srf'" ;;
  page)   adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a 'Page loaded\|EVAL'" ;;
  cookie) adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a -i 'cookie\|431\|token'" ;;
  2fa)    adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a -i '2fa\|post.srf\|auth'" ;;
  wrapper) adb -s "$ADB_SERIAL" shell "tail -f '$LOG' | grep -a -v 'console\.'" ;;
  *)      adb -s "$ADB_SERIAL" shell "tail -f '$LOG'" ;;
esac
