# pnga_ui_qt

Qt UI library (REPOSITORY_LAYOUT.md §4, §8.1). The only Qt-bearing library;
everything under `libs/` stays Qt-free (ADR-0003).

## Responsibility

- Qt models, widgets, panels, canvas and commands that consume backend-neutral
  analysis models.
- WP-104: the Chunk tree model and the windowed Hex view.
- WP-5U1: SelectionBus can publish partial dimension updates while preserving
  independent pixel and Chunk selection dimensions; coordinate summaries come
  from the analysis engine rather than widget-side decoding. `SelectionViewState`
  keeps hover/locked coordinates, HexSource and numeric base as Qt-side state.
- WP-5U2: the application composes a left Chunk dock, central Preview/Hex
  splitter and right coordinate-toolbar/Inspector workspace; layout state is
  persisted through Qt settings with a deterministic reset fallback.
- WP-5U3A: `DeliveredImageView` keeps hover presentation transient and emits
  explicit click, keyboard-nudge and cancel events; locked/hover pixel markers
  are rendered without publishing hover as a domain Selection.
- WP-5U3B/WP-5U9: `StagePixelProcessView` consumes one bounded,
  Qt-free stage-pixel projection for each central Pixels/Filtered/Defiltered
  page; it does not create a second full-size image or decode on the UI thread.
- WP-5U3C remains available as a compatibility component for non-visible stage
  summaries and focused regression tests; the central pages use the shared
  stage-pixel renderer.
- WP-5U4A: `HexDataSource` provides lifetime-safe File and virtual IDAT
  windowed reads; `HexView` no longer borrows a raw ByteSource pointer.
- WP-5U4C: `HexView` keeps bounded address history and supports back/forward
  navigation while safely clipping multiple highlight spans.
- WP-5U5B: `StageInspector` shows a coordinate-driven Reconstruct summary
  backed by the Qt-free view model, including pass/row/sample and filter-step
  values.
- WP-505A: `BlockInspector` renders the Qt-free Deflate block projection with
  BFINAL/BTYPE, input-bit and output-byte ranges, optional scanline/current
  output context, physical IDAT spans and bounded Hex/DEFLATE navigation
  signals. The main workspace exposes it as `DEFLATE / Block`.
- WP-505B: `HuffmanInspector` renders Stored LEN/NLEN, Fixed predefined table
  capacities and Dynamic canonical entries in decoder build order, including
  selected literal and input-bit context. The main workspace exposes it as
  `DEFLATE / Huffman Tables`.
- WP-505C: `DecodeTraceInspector` renders bounded literal/match/EOB steps,
  RFC length/distance base-plus-extra arithmetic, overlap-safe match sources,
  selected output bytes and Hex/DEFLATE range signals. The main workspace
  exposes it as `DEFLATE / Decode Trace`.
- M5 Trace Gate: `TraceInspectorBinding` publishes one generation-consistent
  `TraceQueryResult` projection to all three Deflate pages; bounded
  Trace-to-Original-Literal and navigation semantics remain Qt-free.
- WP-5U6A: `TraceInspectorStateMachine` and `publishState` make loading,
  replaying, partial, error, cancelled and stale-generation states visible
  without allowing an older result to overwrite a newer document.
- WP-5U6B: Block/Huffman/Decode tables and Hex highlights enforce bounded
  rendering caps with deterministic truncation rows; the performance test
  records cold/hot render timings without copying the source file.
- WP-5U6C: the cross-platform GUI gate records host/DPI evidence (default,
  150% and 200% scale runs) and checks reference-size layout, palette changes,
  the Open shortcut, deterministic focus order, every Inspector page's basic
  accessible name and bounded Inspector truncation rows.

## Non-goals

- Parsing PNG data, Inflate or accessing libpng private structures.
- Blocking the UI event loop with file reads or decoding (AGENTS.md).

## Public targets

- `pnga_ui_qt` (alias `pnga::ui_qt`).

## Allowed dependencies

- Approved Qt modules, `pnga_analysis_engine`, `pnga_trace_model`,
  `pnga_rendering` (layout §7 final state).
- Interim (WP-104, until M2's analysis engine lands): `pnga::png_format`,
  `pnga::io`. This deviation is temporary and must be reconciled at M2.
