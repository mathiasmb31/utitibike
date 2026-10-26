# Logo — Utitibike

L'icône de l'application est le **dessin original** du projet :
`logo.png` (256×256), référencé par `Icon=logo.png` dans `utitibike.desktop`
et installé à la racine du click par `CMakeLists.txt`.

## Historique

- En 0.1.4, le logo avait été remplacé par le badge « Vibe coded / Human
  Tested / Human verified » (convention transverse
  `base-de-connaissances/convention-logo-vibe.md`, voir `logo-vibe.svg`).
- **Décision du mainteneur (0.1.5)** : cette convention badge n'est **pas**
  appliquée à Utitibike — ce n'est pas le design souhaité. Le dessin original
  est restauré comme icône : `logo.png` = `logo-old-256.png`, identique au
  `logo.png` de la v0.1.3 (commit `e1b864c`), 256×256.

## Fichiers

| Fichier | Rôle |
|---|---|
| `logo.png` | Icône installée — **dessin original** (256×256). |
| `logo-old-256.png` | Copie locale du dessin original (non suivi, `*.png`). |
| `logo-simple.svg` / `logo-simple.png` | Version simple (vélo blanc sur fond bleu, sans texte) — alternative, non utilisée comme icône. |
| `logo-vibe.svg` / `logo-vibe.png` | Badge « Vibe coded / Human Tested / Human verified » — alternative, **non utilisée** comme icône. |

## Note

La convention transverse « badge vibe » existe dans la base de connaissances
mais n'est **pas** appliquée à ce projet, sur décision du mainteneur.
