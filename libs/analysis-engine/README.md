# pnga_analysis_engine

Orchestration rather than codec algorithms (REPOSITORY_LAYOUT.md §5.10, ADR-0006).

## Responsibility

- Fast Index and on-demand Deep Trace task graphs.
- On-demand pixel/channel provenance queries from native samples to physical
  IDAT bit spans (WP-504).
- Qt-free coordinate summaries that resolve image-global coordinates to
  pass-local rows, stage byte/bit offsets and native sample indices (WP-5U1).
- A bounded, Qt-free Trace Query Contract that composes associated Deflate
  blocks, token/table summaries and logical/physical bit provenance without
  starting a worker or retaining a whole-file token trace (WP-5T0A).
- A cancelable Trace Orchestrator that replays only the requested bounded
  output interval and drops stale document generations before publication
  (WP-5T0B).
- Bounded native-sample viewport queries with a one-request cache for the
  Pixels view (WP-5U3B); viewport requests never allocate a full-size QImage.
- Qt-free reconstruction view models expose pass/row/sample offsets, bounded
  `X/a/b/c` neighbor steps and stable boundary errors (WP-5U5A).
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
