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

## Fichiers

| Fichier | Rôle |
|---|---|
| `logo.png` | Icône installée — **composite** : dessin original centré (disque r=250) + texte circulaire « VIBE CODED • HUMAN TESTED • HUMAN VERIFIED » (r=300), fond bleu `#1278d0` (512×512, coins transparents). |
| `logo-composite.svg` | Source du composite : dessin original embarqué en base64 + texte circulaire (viewBox 800×800). |
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

- texte présent sur toute la circonférence, densité identique au badge de
  référence `logo-vibe.svg` (texte dans la bande radiale [300,355] du
  viewBox 800, rotation SMIL ignorée au rendu statique) ;
- pas de clipping : l'anneau [355,390] entre le texte et le bord du disque
  est resté bleu ;
- dessin original au centre : 97,7 % des pixels du disque central identiques
  au dessin original (à 5 % près) ;
- coins transparents hors disque (r=390).
