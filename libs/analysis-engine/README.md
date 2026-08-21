# pnga_analysis_engine

Orchestration rather than codec algorithms (REPOSITORY_LAYOUT.md §5.10, ADR-0006).

## Responsibility

- Fast Index and on-demand Deep Trace task graphs.
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
