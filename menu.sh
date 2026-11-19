#!/bin/bash
# menu.sh — menu explicite, avec « mode par défaut ».
# Chaque script reçoit, quand c'est possible, les valeurs de son contexte :
#   VERSION (déduite de package.json/manifest.json/CMakeLists.txt),
#   COMMIT_MSG, PR_TITLE, FIX_NAME, DEVICE / ADB_DEVICE, LOG_FILTER.
# Un choix vide lance l'action par défaut du projet (build+install, ou check).
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT" || exit 1

PROJECT="$(basename "$ROOT")"
BRANCH="$(git branch --show-current 2>/dev/null || printf '?')"

version_from_repo() {
  local f v=""
  for f in package.json manifest.json; do
    [ -f "$f" ] || continue
    v=$(grep -m1 '"version"' "$f" 2>/dev/null | sed -E 's/.*"version"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')
    [ -n "$v" ] && { printf '%s' "$v"; return; }
  done
  for f in CMakeLists.txt; do
    [ -f "$f" ] || continue
    v=$(grep -m1 -iE 'project\(.*VERSION|set\([[:space:]]*VERSION' "$f" 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)
    [ -n "$v" ] && { printf '%s' "$v"; return; }
  done
  printf '?'
}

VERSION="${VERSION:-$(version_from_repo)}"
DEVICE="${DEVICE:-${ADB_DEVICE:-<serial>}}"
COMMIT_MSG="${COMMIT_MSG:-}"
PR_TITLE="${PR_TITLE:-}"
FIX_NAME="${FIX_NAME:-}"
LOG_FILTER="${LOG_FILTER:-error}"

desc() {
  case "$1" in
    build-deploy.sh)        echo "BUILD + INSTALL sur $DEVICE + vérif version (défaut: $VERSION)" ;;
    build-fast.sh)          echo "BUILD local sans install — itération rapide (pas de device)" ;;
    build-all.sh)           echo "BUILD de tous les projets (parallélisme plafonné)" ;;
    test-rapide.sh)         echo "TEST device : dernier click → install → lancement → logs/erreurs" ;;
    run.sh)                 echo "RUN de l'app buildée localement, sans click" ;;
    check.sh)               echo "CHECK pré-vol : syntaxe, lint, tests, confidentialité, i18n" ;;
    git-auto.sh)            echo "GIT : status/commit/push/pr/newfix/livraison (sous-menu, valeurs du contexte)" ;;
    bump-version.sh)        echo "VERSION : sync manifest.json + package.json (défaut: $VERSION)" ;;
    watch-log.sh)           echo "LOGS device en direct (filtre défaut: $LOG_FILTER)" ;;
    openstore-prepare.sh)   echo "RELEASE : kit OpenStore, AUCUN upload sans OK explicite (défaut: $VERSION)" ;;
    install-python-deps.sh) echo "OUTILS : installe les dépendances Python du projet" ;;
    release-all.sh)         echo "RELEASE transverse : clicks + notes dans /home/mathias/build" ;;
    docker-optimize.sh)     echo "OUTILS : nettoyage des images Docker" ;;
    fiche-kb.sh)            echo "DOCS : brouillon de fiche base-de-connaissances + INDEX" ;;
    menu.sh)                echo "Ce menu" ;;
    *)                      echo "(script sans description — lancer avec --help)" ;;
  esac
}

default_script() {
  [ -f build-deploy.sh ] && { printf 'build-deploy.sh'; return; }
  [ -f check.sh ] && { printf 'check.sh'; return; }
  printf ''
}

ask() {
  local prompt="$1" def="$2" ans=""
  read -r -p "  $prompt [$def] : " ans || printf '%s' "$def"
  printf '%s' "${ans:-$def}"
}

run_cmd() {
  echo ">>> $*"
  "$@"
}

git_menu() {
  [ -f git-auto.sh ] || return 0
  while true; do
    echo ""
    echo "--- git-auto.sh — $PROJECT @ $BRANCH (version $VERSION) ---"
    echo "  Contexte : COMMIT_MSG='${COMMIT_MSG:-(non défini)}'  PR_TITLE='${PR_TITLE:-(non défini)}'  FIX_NAME='${FIX_NAME:-(non défini)}'"
    echo "  1) status                 (défaut)"
    echo "  2) commit                 (message défaut: ${COMMIT_MSG:-(à saisir)})"
    echo "  3) push                   (branche $BRANCH)"
    echo "  4) pr                     (titre défaut: ${PR_TITLE:-(à saisir)})"
    echo "  5) livraison              (version $VERSION, titre: livraison $PROJECT $VERSION)"
    echo "  6) newfix                 (nom défaut: ${FIX_NAME:-(à saisir)})"
    echo "  0) retour"
    local c msg title name ver
    read -r -p "  Choix [1] : " c || return 0
    case "${c:-1}" in
      0|q) return 0 ;;
      1) run_cmd ./git-auto.sh status ;;
      2) msg=$(ask "message de commit" "$COMMIT_MSG"); [ -n "$msg" ] && run_cmd ./git-auto.sh commit "$msg" ;;
      3) run_cmd ./git-auto.sh push ;;
      4) title=$(ask "titre de la PR" "$PR_TITLE"); [ -n "$title" ] && run_cmd ./git-auto.sh pr "$title" ;;
      5) ver=$(ask "version" "$VERSION"); title=$(ask "titre" "livraison $PROJECT $ver"); run_cmd ./git-auto.sh livraison "$ver" "$title" ;;
      6) name=$(ask "nom du fix" "$FIX_NAME"); [ -n "$name" ] && run_cmd ./git-auto.sh newfix "$name" ;;
      *) echo "  choix invalide" ;;
    esac
  done
}

launch() {
  local s="$1" arg
  case "$s" in
    git-auto.sh) git_menu; return ;;
  esac
  case "$s" in
    bump-version.sh|build-deploy.sh|openstore-prepare.sh)
      arg=$(ask "version" "$VERSION")
      run_cmd ./"$s" "$arg" ;;
    watch-log.sh)
      arg=$(ask "filtre (error|loop|page|cookie|vide)" "$LOG_FILTER")
      run_cmd ./"$s" "$arg" ;;
    *)
      run_cmd ./"$s" ;;
  esac
}

while true; do
  echo ""
  echo "=== Menu $PROJECT ==="
  echo "  Branche: $BRANCH   Version: $VERSION   Device: $DEVICE"
  echo "  Défauts: Entrée = $(default_script) | COMMIT_MSG=${COMMIT_MSG:-(non défini)} | LOG_FILTER=$LOG_FILTER"
  echo ""
  local_i=1
  scripts=()
  for s in *.sh; do
    [ -f "$s" ] || continue
    [ "$s" = "menu.sh" ] && continue
    scripts+=("$s")
    printf "  %2d) %-24s %s\n" "$local_i" "$s" "$(desc "$s")"
    local_i=$((local_i + 1))
  done
  echo "   0) quitter"
  echo ""
  local_choice=""
  read -r -p "Choix (numéro, 'h <script>', Entrée = défaut) : " local_choice || exit 0
  case "$local_choice" in
    0|q|quit) exit 0 ;;
    '')
      d=$(default_script)
      if [ -n "$d" ]; then echo ">>> action par défaut: $d"; launch "$d"; else echo "aucune action par défaut"; fi ;;
    h:*|h\ *)
      s="${local_choice#h:}"; s="${s# }"
      [ -f "$s" ] && run_cmd ./"$s" --help || echo "script inconnu: $s" ;;
    h)
      for s in "${scripts[@]}"; do echo "--- $s"; ./"$s" --help 2>/dev/null | head -5; done ;;
    *[!0-9]*) echo "choix invalide (numéro attendu)" ;;
    *)
      n=$((local_choice))
      if [ "$n" -ge 1 ] && [ "$n" -le "${#scripts[@]}" ]; then
        launch "${scripts[$((n-1))]}"
      else
        echo "choix invalide"
      fi ;;
  esac
done
