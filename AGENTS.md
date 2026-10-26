# AGENTS.md — Règles de travail

## Branches (OBLIGATOIRE)

**Ne jamais committer directement sur `main`.**

Toujours travailler dans une branche dédiée :

| Type | Format | Usage |
|------|--------|-------|
| Feature | `feature/<nom>` | Nouvelle fonctionnalité |
| Fix | `fix/<nom>` | Correction de bug |
| Hotfix | `hotfix/<nom>` | Correction urgente |

### Workflow

```bash
# Créer une branche depuis main
git checkout main && git pull
git checkout -b feature/ma-fonctionnalite

# Travailler, committer, push
# ...

# Merger sur main via Pull Request (pas de push direct sur main)
```

### CI

- Les PR vers `main` déclenchent la CI (tests + lint + build)
- Ne merger que si la CI est verte
