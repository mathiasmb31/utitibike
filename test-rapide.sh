#!/bin/bash
# Test rapide mathias.utitibike sur le téléphone (adb <serial>)
set -e

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: ./test-rapide.sh"
  echo "  Tue les vieux process, lance l'app, affiche: version, process, heartbeat, logs récents"
  exit 0
fi

DEV="<serial>"
APPID="mathias.utitibike"
PROC="utitibike"

echo "=== Version installée ==="
adb -s $DEV shell "click info $APPID | grep version" 2>/dev/null || echo "adb non connecté"

echo "=== Kill vieux process ==="
echo "=== Zombies avant kill ==="
adb -s $DEV shell "ps -eo pid,stat,comm | awk '\$2 ~ /Z/ {z++} END {print \"zombies: \" (z+0)}'; echo \"process avant kill: \$(ps aux | grep $PROC | grep -v grep | wc -l)\""

adb -s $DEV shell "ps aux | grep $PROC | grep -v grep | awk '{{print \$2}}' | while read p; do kill -9 $p 2>/dev/null; done; sleep 2; echo reste: $(ps aux | grep $PROC | grep -v grep | wc -l)"

echo "=== Lancement ==="
adb -s $DEV shell "setsid lomiri-app-launch $APPID > /dev/null 2>&1 &"
sleep 15

echo "=== Process (doit être > 0) ==="
adb -s $DEV shell "ps aux | grep $PROC | grep -v grep | wc -l"

echo "=== Derniers logs ==="
adb -s $DEV shell "logcat -d 2>/dev/null | grep -iE '$PROC.*(error|fatal|crash)' | tail -5" || echo "pas de logcat"
echo "=== FIN ==="
