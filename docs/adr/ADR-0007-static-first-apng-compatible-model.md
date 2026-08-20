# ADR-0007: Deliver Static PNG First With An APNG-Compatible Model

- Status: Accepted
- Date: 2026-08-20

## Context

Static PNG reconstruction is already a substantial correctness surface. APNG adds frame streams, sequence validation, blend/dispose operations and multiple canvas stages. Retrofitting frame identity after v1 would destabilize selections, artifacts and provenance.

## Decision

The v1 implementation targets static PNG. From the first public model, document, selection, stage and coordinate types may represent frame identity and frame-local artifacts. Static images use the default frame without requiring APNG decoding behavior.

APNG parsing, frame decode and canvas composition are deferred to dedicated post-v1 Work Packages. Static feature tasks must not implement partial APNG semantics outside those packages.

## Consequences

- Early implementation and testing remain focused on static correctness.
- Public models avoid an incompatible frame-dimension retrofit.
- Some frame-related fields may remain empty for all v1 artifacts.
- APNG support still requires explicit validation and composition stages; compatibility in the model does not imply feature support.
