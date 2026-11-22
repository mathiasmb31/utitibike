#!/bin/bash
# batterie-tests.sh — batterie de tests device + host pour UT WheelTrek (velo-touch)
# Usage : ./batterie-tests.sh [version]
#   ./batterie-tests.sh          (lit la version dans manifest.json)
#
# Prérequis : téléphone branché (device défini par ADB_DEVICE), app installée, build-host présent.
# NE touche qu'à utitibike — aucun appel/SMS, aucune autre app, pas de reboot.
# Le rapport device est écrit dans ~/.config/mathias.utitibike/batterie.log.

set +e
VERSION="${1:-$(python3 -c "import json;print(json.load(open('$(dirname "$0")/manifest.json'))['version'])" 2>/dev/null)}"
DEV="${ADB_DEVICE:-${ADB_SERIAL:-$(adb devices 2>/dev/null | awk 'NR==2 {print $1}')}}"
ADB="adb -s $DEV"
APPID="mathias.utitibike_utitibike_${VERSION}"
PROC="utitibike"
DIR="/home/phablet/.local/share/mathias.utitibike"
LOG="/home/phablet/.config/mathias.utitibike/batterie.log"
TMP="/home/phablet/.config/mathias.utitibike/ping.tmp"

echo "=== Batterie UT WheelTrek $VERSION (device $DEV) ==="

# ---------------------------------------------------------------- host
echo "=== HOST: tests C++ (ctest) ==="
if [ -f "$(dirname "$0")/build-host/CMakeCache.txt" ]; then
  (cd "$(dirname "$0")/build-host" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --timeout 60 2>&1 | tail -12)
else
  echo "build-host absent — lancer cmake -DBUILD_TESTING=ON + build d'abord"
fi

# ---------------------------------------------------------------- prepare
$ADB shell "mkdir -p /home/phablet/.config/mathias.utitibike; pkill -9 -x $PROC 2>/dev/null; sleep 3" >/dev/null 2>&1

$ADB shell "cat > /home/phablet/.config/mathias.utitibike/.batterie-run.sh <<'EOS'
#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/32011
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus
D=$DIR
L=$LOG
: > \"\$L\"
echo \"VERSION: \$(click list 2>/dev/null | grep -i utitibike)\" >> \"\$L\"

echo '=== TEST 1: lancement ===' >> \"\$L\"
lomiri-app-launch $APPID >> \"\$L\" 2>&1
sleep 10
echo -n 'PROCESS=' >> \"\$L\"; ps -eo comm | grep -cx $PROC >> \"\$L\"
echo -n 'FENETRE=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -c 'utitibike window' >> \"\$L\"
echo -n 'CRASH/FATAL=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -icE 'FATAL|segfault|abort' >> \"\$L\"
echo -n 'TRADuction fr=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -c 'translation loaded for locale' >> \"\$L\"

echo '=== TEST 2: icone jaune ===' >> \"\$L\"
echo -n 'unknown.qtmir=' >> \"\$L\"; gdbus introspect --session --dest com.lomiri.Shell --object-path /com/lomiri/Shell/Launcher --xml 2>/dev/null | grep -c unknown_2Eqtmir >> \"\$L\"

echo '=== TEST 3: repertoires de donnees ===' >> \"\$L\"
echo -n 'tiles/osm=' >> \"\$L\"; [ -d \"\$D/tiles/osm\" ] && echo 1 >> \"\$L\" || echo 0 >> \"\$L\"
echo -n 'qmlcache=' >> \"\$L\"; find /home/phablet/.cache/mathias.utitibike -name '*.qmlc' 2>/dev/null | wc -l >> \"\$L\"
echo -n 'appDataDir=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -m1 'tilecache base' >> \"\$L\"

echo '=== TEST 4: AppArmor ===' >> \"\$L\"
echo -n 'DENIED NetworkManager=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -c 'NetworkManager' >> \"\$L\"
echo -n 'DENIED libertine=' >> \"\$L\"; journalctl --user --since '-1 min' --no-pager 2>/dev/null | grep -c 'libertine.Service.Operations' >> \"\$L\"

echo '=== TEST 5: cycles quit/relance ===' >> \"\$L\"
for c in 1 2 3; do
  echo \"--- cycle \$c \$(date +%H:%M:%S)\" >> \"\$L\"
  echo -n 'avant-quit=' >> \"\$L\"; ps -eo comm | grep -cx $PROC >> \"\$L\"
  pkill -9 -x $PROC 2>/dev/null || true
  sleep 3
  lomiri-app-launch $APPID >> \"\$L\" 2>&1
  sleep 10
  echo -n 'apres-relance=' >> \"\$L\"; ps -eo comm | grep -cx $PROC >> \"\$L\"
done

echo '=== TEST 6: etat final ===' >> \"\$L\"
echo -n 'unknown final=' >> \"\$L\"; gdbus introspect --session --dest com.lomiri.Shell --object-path /com/lomiri/Shell/Launcher --xml 2>/dev/null | grep -c unknown_2Eqtmir >> \"\$L\"
echo -n 'zombies=' >> \"\$L\"; ps -eo stat | grep -c Z >> \"\$L\"
echo -n 'process final=' >> \"\$L\"; ps -eo comm | grep -cx $PROC >> \"\$L\"
echo '=== FIN ===' >> \"\$L\"
EOS
chmod +x /home/phablet/.config/mathias.utitibike/.batterie-run.sh
nohup /home/phablet/.config/mathias.utitibike/.batterie-run.sh >/dev/null 2>&1 &
echo LANCÉ"

echo "Batterie device lancée. Suivi :"
echo "  $ADB shell 'cat $LOG'"
