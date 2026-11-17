#!/bin/bash
# menu.sh — lance les scripts du projet depuis un menu explicatif
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

desc() {
  case "$1" in
    build-deploy.sh)       echo "Build l'app (clickable, arm64) + install sur le device adb <serial> + vérif version installée" ;;
    build-fast.sh)         echo "Build rapide sans install (pour itérer localement)" ;;
    test-rapide.sh)        echo "Test rapide sur le device : install du dernier .click + lancement + capture logs/errors" ;;
    run.sh)                echo "Lance l'app buildée localement sans install click" ;;
    git-auto.sh)           echo "Workflow git : status / commit / push / pr / newfix / livraison <version> (voir --help)" ;;
    bump-version.sh)       echo "Sync la version dans manifest.json + package.json : ./bump-version.sh <X.Y.Z>" ;;
    check.sh)              echo "Toutes les vérifications pre-commit : syntaxe scripts + src, tests, grep <NOM-PROSCRIT>, grep i18n" ;;
    watch-log.sh)          echo "Tail live des logs de l'app sur le device (filtres: error|loop|page|cookie|2fa|wrapper)" ;;
    openstore-prepare.sh)  echo "Prépare le kit de publication OpenStore (whatsnew auto depuis git, metadata, screenshots) — AUCUN upload sans OK explicite" ;;
    install-python-deps.sh) echo "Installe les dépendances python du projet" ;;
    menu.sh)               echo "Ce menu" ;;
    *)                     echo "(script sans description — lancer avec --help)" ;;
  esac
}

while true; do
  echo ""
  echo "=== Menu $ROOT ==="
  i=1
  scripts=()
  for s in *.sh; do
    [ -f "$s" ] || continue
    [ "$s" = "menu.sh" ] && continue
    scripts+=("$s")
    printf "  %d) %-24s %s\n" "$i" "$s" "$(desc "$s")"
    i=$((i+1))
  done
  echo "  0) quitter"
  echo ""
  read -r -p "Choix (numéro, ou 'h <script>' pour son --help) : " choice
  case "$choice" in
    0|q|quit) exit 0 ;;
    h:*|h\ *) s="${choice#h:}"; s="${s# }"; [ -f "$s" ] && "$s" --help || echo "script inconnu: $s" ;;
    h) for s in "${scripts[@]}"; do echo "--- $s"; "$s" --help 2>/dev/null | head -5; done ;;
    ''|*[!0-9]*) echo "choix invalide" ;;
    *)
      n=$((choice))
      if [ "$n" -ge 1 ] && [ "$n" -le "${#scripts[@]}" ]; then
        s="${scripts[$((n-1))]}"
        echo ">>> lancement de $s"
        if [ "$s" = "git-auto.sh" ] || [ "$s" = "bump-version.sh" ] || [ "$s" = "watch-log.sh" ] || [ "$s" = "openstore-prepare.sh" ]; then
          read -r -p "  args pour $s (ex: commit "fix: ..." ou 4.0.62) : " args
          # shellcheck disable=SC2086
          ./"$s" $args
        else
          ./"$s"
        fi
      else
        echo "choix invalide"
      fi
      ;;
  esac
done
