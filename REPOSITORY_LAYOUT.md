# PNG Analyzer Repository Layout Contract

Document ID: REPOSITORY-LAYOUT-0001
Version: 0.1
Status: Accepted
Last updated: 2026-08-20

This root file is the canonical entry point for repository structure and module ownership. The complete normative contract is maintained in [`docs/architecture/REPOSITORY_LAYOUT.md`](docs/architecture/REPOSITORY_LAYOUT.md). Contributors and agents must read that document and `AGENTS.md` before creating, moving, renaming or deleting source directories.

## Binding Rules

- Source modules use the canonical `apps/`, `libs/`, `ui/qt/`, `tests/`, `docs/`, `third_party/`, `packaging/` and `cmake/` ownership described by the complete contract.
- Directories are created only when an approved Work Package first needs them; empty future directories are not scaffolding.
- Everything under `libs/` is Qt-free. PNG, Deflate and reconstruction algorithms never belong in GUI targets.
- `libs/backend-libpng` is the only production libpng consumer. Fast Inflate, random-access indexing and Deep Trace remain distinct modules.
- Public library headers live under `include/pnga/<module>/`; private headers remain under `src/`.
- The dependency graph is acyclic and follows the allowed target table in the complete contract.
- External corpus files and vendored sources require provenance, license and hashes before entering the repository.
- Conflicting legacy paths use the canonical replacements in Section 10 of the complete contract.

Changing module ownership, dependency direction, a top-level source directory, the Qt boundary, package manager or plugin ABI requires a dedicated ADR and an update to the complete contract before implementation.
