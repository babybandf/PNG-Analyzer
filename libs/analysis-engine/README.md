# pnga_analysis_engine

Orchestration rather than codec algorithms (REPOSITORY_LAYOUT.md §5.10, ADR-0006).

## Responsibility

- Fast Index and on-demand Deep Trace task graphs.
- On-demand pixel/channel provenance queries from native samples to physical
  IDAT bit spans (WP-504).
- Qt-free coordinate summaries that resolve image-global coordinates to
  pass-local rows, stage byte/bit offsets and native sample indices (WP-5U1).
- Immutable per-frame `AnalysisTarget` contexts for APNG inspection:
  `make_frame_target` builds the frame-scoped virtual stream, delivery
  context and header; `frame_local_point` maps canvas-global points into the
  frame rectangle with checked arithmetic; `query_frame_coordinate` resolves
  canvas-global selections against analyzed frame stages and restores
  canvas-global coordinates in its output (WP-APNG-INSPECT).
- Generic virtual-compressed-stream overloads of the shared row/trace
  kernels so frame streams reuse the static implementations without an
  adapter: `inflate_filtered`, `build_scanline_anchors`, `restore_scanline`
  and `query_pixel_provenance` accept any `IVirtualCompressedStream`
  (WP-APNG-INSPECT). Original source-pair overloads remain.
- `QueryCoordinator::open(target, anchor_interval_bytes)` builds the anchor
  index over an `AnalysisTarget` frame stream; the document generation
  adopts `target->key.generation` (WP-APNG-INSPECT).
- A bounded, Qt-free Trace Query Contract that composes associated Deflate
  blocks, token/table summaries and logical/physical bit provenance without
  starting a worker or retaining a whole-file token trace (WP-5T0A).
- A cancelable Trace Orchestrator that replays only the requested bounded
  output interval and drops stale document generations before publication
  (WP-5T0B).
- Bounded native-sample viewport queries with a one-request cache for the
  Pixels view (WP-5U3B); viewport requests never allocate a full-size QImage.
- Bounded stage pixel-process projections for the central Pixels, Filtered and
  Defiltered views (WP-5U9); the query returns fixed-neighborhood facts and
  filter events without Qt or decoder work.
- Qt-free reconstruction view models expose pass/row/sample offsets, bounded
  `X/a/b/c` neighbor steps and stable boundary errors (WP-5U5A).
- WP-602A adapts immutable Chunk, Stage, Deflate block and token results into
  the bounded `pnga_statistics` scalar sample contract without copying payloads
  or adding decoder logic.
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
