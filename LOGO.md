# Logo — Utitibike

L'icône de l'application est un **composite** : le **dessin original** du
projet au centre, entouré du **texte circulaire** « VIBE CODED • HUMAN TESTED
• HUMAN VERIFIED » (convention transverse
`base-de-connaissances/convention-logo-vibe.md`).

`logo.png` (512×512, coins transparents), référencé par `Icon=logo.png` dans
`utitibike.desktop` et installé à la racine du click par `CMakeLists.txt`.

## Historique

- Jusqu'en 0.1.3 : `logo.png` = dessin original seul (256×256).
- En 0.1.4 : le logo avait été remplacé par le badge « Vibe coded / Human
  Tested / Human verified » **seul** (glyph vélo, voir `logo-vibe.svg`) — le
  dessin original avait été écrasé.
- 0.1.5 : restauration temporaire du dessin original seul.
- **Décision du mainteneur (0.1.6)** : le dessin original est restauré **et**
  le texte circulaire du badge est conservé autour. `logo.png` = composite
  dessin original + texte circulaire, généré depuis `logo-composite.svg`.
- **0.1.7** : agrandissement demandé (« le dessin est trop petit ») — dessin
  central r=250 → r=290 (500×500 → 580×580) et texte circulaire agrandi
  (font-size 74 → 78, path R=300 → R=316). Anneau décoratif déplacé à
  r=378, points cardinaux à r=384. Disque de fond r=390 inchangé.

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
- **0.1.7** (agrandissement) — `verif-logo.sh logo.png 316 78` :
  - pas de clipping : trou net au secteur 11 (165–180°, fin du texte) = 14,
    bande de texte contenue dans [316,372] (profil radial mesuré [316,371]) ;
  - lisibilité améliorée : encre textuelle à 128 px = 1141 (était 1049,
    +8,8 %), 96 px = 662 (était 623), 64 px = 272 (était 268) ;
  - dessin central agrandi : contenu dessiné jusqu'à ~275 unités de rayon
    (était ~230), séparateur blanc r=252 → r=292, image 500×500 → 580×580 ;
  - anneaux conservés : anneau bleu intérieur [294,316] = 22 unités, anneau
    extérieur [372,390] = 17 unités (anneau décoratif r=378, points
    cardinaux r=384) ;
  - le base64 embarqué dans le SVG est byte-identique à `logo-old-256.png`
    (md5 `09c6ccd5d4fd0fbb2a48111c97f42e4c`) ;
  - disque de fond r=390 inchangé, coins transparents hors disque (vérifiés
    sur les 4 coins, alpha=0).
