# ADR-0008: Use Only Public libpng Interfaces

- Status: Accepted
- Date: 2026-08-20

## Context

libpng intentionally hides implementation structures and does not guarantee private field or internal function stability. Direct access would couple the analyzer to one source revision and undermine the Reference Backend's role as an independent oracle.

## Decision

Production integration uses documented public libpng read, callback, transform, limit and error APIs only. Access to private `png_struct` or `png_info` fields, copied internal functions and patches to the installed libpng source are forbidden.

If observation of libpng internals later becomes a product requirement, it must use an optional, exact-version instrumented backend approved by a separate ADR. The normal Reference Backend remains public-API-only.

## Consequences

- Dependency upgrades remain tractable and the reference path stays independently maintained.
- Internal Deflate and reverse-filter states come from the Trace Backend, not fabricated libpng artifacts.
- libpng error/longjmp behavior must be contained inside the backend boundary.
- Backend capability reporting must distinguish delivered rows from unavailable internal stages.
