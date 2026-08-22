# pnga_analysis_engine

Orchestration rather than codec algorithms (REPOSITORY_LAYOUT.md §5.10, ADR-0006).

## Responsibility

- Fast Index and on-demand Deep Trace task graphs.
- On-demand pixel/channel provenance queries from native samples to physical
  IDAT bit spans (WP-504).
- Qt-free coordinate summaries that resolve image-global coordinates to
  pass-local rows, stage byte/bit offsets and native sample indices (WP-5U1).
- Worker scheduling, cancellation and generation IDs (later WP).
- Artifact cache, memory budgets and stale-result suppression.
- Backend selection and publication of immutable results.

## Non-goals

- PNG/Deflate decoding or filter algorithms.
- Qt models or GUI objects.

## Public targets

- `pnga_analysis_engine` (alias `pnga::analysis_engine`).

## Allowed dependencies

- Approved libraries (`pnga_trace_model`, ...). Never Qt (ADR-0003).
