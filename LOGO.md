# Logo — convention « Vibe coded / Human Tested / Human verified »

Ce projet doit suivre la **convention logo transverse** :
`../base-de-connaissances/convention-logo-vibe.md`

En résumé :

- Créer `logo-vibe.svg` : cercle fond **couleur primaire d'Utitibike**, glyphe blanc
  central (symbole vélo / carte), texte circulaire blanc gras
  « VIBE CODED • HUMAN TESTED • HUMAN VERIFIED • » en rotation (SMIL 20 s),
  une seule répétition, font-size nominal ~74, path r=300
- `logo.png` (icône installée, 512×512, `Icon=logo.png` dans `utitibike.desktop`,
  installé par CMakeLists.txt) = rendu **librsvg** de `logo-vibe.svg`
  (`convert -density 150 ... -resize 512x512`), jamais Inkscape
- Conserver l'ancien logo en `logo-old-*.png`, la version simple en
  `logo.svg` / `logo-simple.png`
- Vérification : `../base-de-connaissances/scripts/verif-logo.sh logo.png 300 74`
- Bump de version à chaque changement de logo

**État : à faire** — seul `logo.png` 256×256 (version simple) existe aujourd'hui.
Template à adapter : `../mail-touch/logo-vibe.svg`.
