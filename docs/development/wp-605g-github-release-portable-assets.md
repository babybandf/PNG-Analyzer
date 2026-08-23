# WP-605G — GitHub Release Portable Assets

Status: **planned; scope frozen** (2026-08-23)

## Goal

Make the already-verified CPack portable archives directly downloadable from a
GitHub Release. The Windows ZIP is required; macOS/Linux TGZ archives are
published alongside it for symmetry.

## Scope and invariants

- Run only for an existing `v*` tag, or manual dispatch naming an existing tag.
- Build the `release` preset on Ubuntu, macOS and Windows using the pinned
  repository bootstrap/dependency workflow.
- Upload exactly one CPack portable archive per platform as Release assets:
  Windows ZIP, macOS TGZ and Linux TGZ.
- Keep the existing Actions artifacts and package smoke gates unchanged.
- Publish with the runner-provided GitHub CLI and `GITHUB_TOKEN`; do not add a
  package manager or an unpinned release action.
- Do not create or push tags automatically. Signing, notarization, MSIX and
  GUI framework deployment remain separate release work.

## Non-goals

Release notes policy, cryptographic signatures/checksums, native installers,
and release approval/tag creation are outside this work package.

## Cheapest discriminating verification

```text
python3 scripts/verify_repository_layout.py
git diff --check
```

The workflow definition must pass repository/layout audits and its publish job
must fail closed when the requested tag does not exist.
