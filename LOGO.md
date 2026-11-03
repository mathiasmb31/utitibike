# Logo — Utitibike

L'icône de l'application est un **composite** : le **dessin original** du
projet au centre, entouré du **texte circulaire** « VIBE CODED • HUMAN TESTED
• HUMAN VERIFIED » (convention transverse
`base-de-connaissances/convention-logo-vibe.md`).

`logo.png` (512×512, coins transparents), référencé par `Icon=logo.png` dans
`utitibike.desktop` et installé à la racine du click par `CMakeLists.txt`.

## Historique

- **0.1.21** : retour du **dessin original** (cycliste, chaussures de rando,
  montagne en arrière-plan — `logo-old-256.png`) dans le look actuel 0.1.18 :
  squircle à coins transparents, dégradé diagonal orange Ubuntu `#E95420` →
  aubergine `#772953`, dessin rendu **en blanc** (alpha modulé par la
  luminance du dessin : `0,55 + 0,45·(1−lum)`, le cycliste sombre ressort
  plus que la montagne claire). Le SVG source du look 0.1.18 ayant été perdu
  (seul le PNG était commité), la source est restaurée dans
  **`logo-ubuntu.svg`** (squircle + dégradé + dessin blanchi embarqué en
  base64). `logo.png` sert à la fois d'**icône** (`Icon=logo.png`) et de
  **splash in-app** (`qrc:/logo.png` dans `qml/Main.qml`). Génération :
  script PIL (fond = masque alpha du logo 0.1.18 + dégradé fit #E95420→
  #772953 ; dessin 256→320 px centré en (96,92)), rendu vérifié : coins
  alpha=0, dégradé (100,100)≈#E95420 / (410,410)≈#772953, ~39 kpx blancs
  centrés. Le rendu inkscape de `logo-ubuntu.svg` donne le même masque à
  99,9 %.
- Jusqu'en 0.1.3 : `logo.png` = dessin original seul (256×256).
- En 0.1.4 : le logo avait été remplacé par le badge « Vibe coded / Human
  Tested / Human verified » **seul** (glyph vélo, voir `logo-vibe.svg`) — le
  dessin original avait été écrasé.
- 0.1.5 : restauration temporaire du dessin original seul.
- **Décision du mainteneur (0.1.6)** : le dessin original est restauré **et**
  le texte circulaire du badge est conservé autour. `logo.png` = composite
  dessin original + texte circulaire, généré depuis `logo-composite.svg`.
- **0.1.7** : agrandissement demandé (« le dessin est trop petit », puis
  « pousse le texte plus loin ») — dessin central r=250 → r=290 (500×500 →
  580×580) et texte circulaire agrandi au maximum (font-size 74 → 80,
  path R=300 → R=325, bande [325,385]). L'anneau décoratif (r=378) et les
  points cardinaux (r=384) sont **supprimés** : plus de place entre la bande
  de texte et le bord du disque (filet bleu résiduel ~5 unités).
  Disque de fond r=390 inchangé.
- **0.1.9** : police condensée **Liberation Sans Narrow Bold** — le texte
  est 18,75 % plus grand (F=80 → 95) sans dépasser le disque. Path R=325 →
  R=320 (bande [320,389], dans le disque r=390). Gains de lisibilité mesurés
  (`verif-logo.sh logo.png 320 95`) : 128 px = 1707 (+34 %), 96 px = 949
  (+46 %), 64 px = **528 (+73 %)** — taille d'affichage tiroir d'apps.
  Trou secteur 11 = 522 (pas de clipping). Police de fallback :
  `Ubuntu Condensed` puis `Ubuntu` puis `sans-serif`.

## Fichiers

| Fichier | Rôle |
|---|---|
| `logo.png` | Icône installée — **composite** : dessin original centré (disque r=290) + texte circulaire « VIBE CODED • HUMAN TESTED • HUMAN VERIFIED » (path R=316, font-size 78), fond bleu `#1278d0` (512×512, coins transparents). |
| `logo-composite.svg` | Source du composite : dessin original embarqué en base64 (identique à `logo-old-256.png`, md5 vérifié) + texte circulaire (viewBox 800×800). |
| `logo-old-256.png` | Copie locale du dessin original 256×256 (non suivi, `*.png`). |
| `logo-simple.svg` / `logo-simple.png` | Version simple (vélo blanc sur fond bleu, sans texte) — alternative, non utilisée comme icône. |
| `logo-vibe.svg` / `logo-vibe.png` | Badge « Vibe coded / Human Tested / Human verified » avec glyph vélo — alternative, non utilisée comme icône. |

## Rendu

Rendu via ImageMagick (moteur SVG interne), jamais Inkscape :

```sh
convert -background none -density 150 logo-composite.svg -resize 512x512 PNG32:logo.png
```

`-background none` est indispensable : sans lui, le moteur SVG aplatit les
coins sur blanc opaque (l'icône doit garder ses coins transparents).
`PNG32:` force la conservation du canal alpha.

Contrôles effectués sur le rendu 512×512 :

- **0.1.6** — texte présent sur toute la circonférence, densité identique au
  badge de référence `logo-vibe.svg` (texte dans la bande radiale [300,355],
  rotation SMIL ignorée au rendu statique) ; pas de clipping : l'anneau
  [355,390] entre le texte et le bord du disque est resté bleu ; dessin
  original au centre : 97,7 % des pixels du disque central identiques au
  dessin original (à 5 % près) ; coins transparents hors disque (r=390).
- **0.1.7** (agrandissement, texte poussé au maximum) —
  `verif-logo.sh logo.png 325 80` :
  - pas de clipping : trou au secteur 11 (165–180°, fin du texte) = 58,
    bande de texte contenue dans [325,385] (profil radial mesuré, le
    résidu [385,390] = 170 est le bord anti-aliasé du disque, identique
    au 0.1.6) ;
  - lisibilité améliorée : encre textuelle à 128 px = 1274 (était 1049
    en 0.1.6, **+21,4 %**), 96 px = 652 (était 623), 64 px = 305
    (était 268) ;
  - dessin central agrandi : contenu dessiné jusqu'à ~275 unités de rayon
    (était ~230), séparateur blanc r=252 → r=292, image 500×500 → 580×580 ;
  - anneau bleu intérieur [294,325] = 31 unités ; anneau extérieur réduit
    à ~5 unités (filet) — l'anneau décoratif et les points cardinaux ont
    été supprimés (plus de place) ;
  - **limite atteinte** : F=81/R=327 testé et rejeté — les glyphes
    débordaient radialement hors du disque r=390 (artefacts visibles en
    aperçu 64×64). F=80/R=325 est le maximum sans débordement (0 pixel
    avec alpha > 10 % hors disque, bandes 4×6 px sur les 4 côtés) ;
  - le base64 embarqué dans le SVG est byte-identique à `logo-old-256.png`
    (md5 `09c6ccd5d4fd0fbb2a48111c97f42e4c`) ;
  - disque de fond r=390 inchangé, coins transparents hors disque (vérifiés
    sur les 4 coins, alpha=0) ;
  - **correction** : le premier commit 0.1.7 embarquait un SVG incohérent
    (font-size resté à 74 au lieu de 78 — édition perdue) alors que le
    PNG rendu correspondait à F=78. Corrigé dans le commit suivant : le SVG
    et le PNG de la branche sont cohérents (F=80, R=325).

## 2026-11-03 — dessin agrandi et renforcé
Sur demande du mainteneur (« le vélo et la scène sont peu visibles ») :
- dessin central **320 → 400 px** (centré en 56,56 au lieu de 320 px en 96,92)
- blanchiment renforcé : alpha `0,55 + 0,45·(1−lum)` → **`0,82 + 0,18·(1−lum)`**
  (le cycliste et la montagne ressortent nettement plus sur le dégradé)
- masque alpha du squircle réutilisé tel quel (diff 0 px avec l'ancien logo),
  dégradé inchangé (#E95420 → #772953), coins transparents conservés
- régénéré en PIL depuis `logo-old-256.png` ; `logo-ubuntu.svg` mis à jour
  (base64 + x/y/width/height). Aperçu 256 : scratchpad `logo-apercu-256.png`.
- pixels "dessin" (échantillon 1/2) : 9686 → 30056, soit ~3× plus visible.
