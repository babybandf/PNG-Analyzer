# pnga_core

Small, domain-independent primitives shared by every other module
(REPOSITORY_LAYOUT.md §5.1, ADR-0003).

## Responsibility

- Error and Result types.
- Stable IDs and handles.
- `SourceSpan`, byte/bit ranges and checked arithmetic.
- Cancellation and progress primitives that do not create threads themselves.
- Build/version information shared by CLI and GUI.

## Non-goals

- PNG, Deflate, filesystem, cache and GUI behavior.
- Becoming a catch-all `utils` directory.

## Public targets

- `pnga_core` (alias `pnga::core`).

## Allowed dependencies

- C++ standard library only. Never Qt (ADR-0003).
