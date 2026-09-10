# pnga_trace_model

Backend-neutral analysis data model (REPOSITORY_LAYOUT.md §5.4, ADR-0004).

## Responsibility

- `SemanticNode` and stable `NodeId`.
- `StageArtifact` and stage identifiers (later WP).
- `Selection` with spans and image coordinates. A missing image channel means
  whole-pixel selection; optional sample-byte and packed-bit fields distinguish
  channel/sample granularity without a Qt-specific coordinate type.
- `ProvenanceSpan` spaces for reversible pixel/stage/file-bit mappings (WP-504).
- `AnalysisKey`, `InspectionTicket` and `accepts_publication`: identity and
  publication-scope rules for per-frame inspection sessions. Target-scoped
  publications match key and target epoch; pixel-scoped publications
  additionally match stage and selection serial (WP-APNG-INSPECT).
- Structured diagnostics and analysis events.

## Non-goals

- Calling a decoder or depending on Qt models.

## Public targets

- `pnga_trace_model` (alias `pnga::trace_model`).

## Allowed dependencies

- `pnga_core`. Never Qt (ADR-0003).
