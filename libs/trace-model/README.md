# pnga_trace_model

Backend-neutral analysis data model (REPOSITORY_LAYOUT.md §5.4, ADR-0004).

## Responsibility

- `SemanticNode` and stable `NodeId`.
- `StageArtifact` and stage identifiers (later WP).
- `Selection` with spans and image coordinates.
- `Provenance` and reversible coordinate mapping records (later WP).
- Structured diagnostics and analysis events.

## Non-goals

- Calling a decoder or depending on Qt models.

## Public targets

- `pnga_trace_model` (alias `pnga::trace_model`).

## Allowed dependencies

- `pnga_core`. Never Qt (ADR-0003).
