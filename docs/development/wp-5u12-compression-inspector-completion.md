# WP-5U12 Completion — Product Compression Inspector

Status: **design approved; pending written-package review** (2026-09-01)

Normative UI contract: `wp-5u12-compression-inspector-flow-ui.md` section 20.
This document converts that audited contract into ordered coding increments.

## Goal and invariants

Complete a product-level, bounded Compression Inspector that answers Block
structure, Huffman construction and token decode questions without generating
or retaining a default whole-file token trace.

- One generation-level Fast Compression Index owns wrapper/IDAT/Adler summary
  and the complete Block list.
- Selection-level Deep Trace owns bounded codebooks/tokens and remains
  cancelable, budgeted and generation-safe.
- Offset domains are explicit: File bytes, zlib-stream bytes, DEFLATE-payload
  bits and Inflated bytes are never interchangeable.
- Logical input spanning multiple IDAT chunks retains every physical span.
- GUI formats immutable facts; it does not parse, Inflate or reverse bit order.

## Dependencies

- WP-5U15: PASS.
- WP-607C controlled corpus may proceed in parallel, but its fixtures must be
  available before WP-5U12F.
- Existing WP-5T0A/B, WP-505A/B/C and WP-5U13 remain authoritative.

## Allowed paths

- `libs/analysis-engine/**`
- focused changes in `libs/deflate-index/**` and `libs/deflate-trace/**` only
  when required to expose an already-computed structured fact
- `ui/qt/**`, `apps/png-analyzer-gui/**`
- focused unit/GUI/integration/performance tests
- this document and the normative WP-5U12 document

## Forbidden paths

- `third_party/**`, PNG parser/filter/reconstruction behavior, packaging
- private zlib/libpng structures or libpng links outside the approved backend
- whole-file token/event retention, IDAT concatenation or unbounded GUI tables
- debug-string parsing, hard-coded demo rows or first-span-only navigation

## WP-5U12A — Offset and Fast Index contract

Define typed ranges for the four offset domains. Extend
`FastCompressionIndexView` with stable wrapper fields, all IDAT physical spans,
DEFLATE payload origin, expected/actual Adler status, completion status and the
complete Block list. Block ranges use one documented origin; token ranges carry
their own domain and may not be compared until normalized by analysis-engine.

Tests: header boundaries, one/many IDAT, Stored/Fixed/Dynamic, cross-IDAT Block,
Adler match/mismatch/not-computed, truncated stream, reserved BTYPE and exact
half-open ranges.

## WP-5U12B — Selection and navigation contract

Introduce a typed navigation value containing `generation`, source domain,
logical range, optional bit range and all physical spans. Separate immutable
Current mapping from Manual Selection. Page selection never changes current
pixel context; current context never silently clears manual selection.

Tests: complete multi-span round-trip, stale generation rejection, Current +
Selection coexistence, navigation loop suppression, File/Frame-independent
source units and correct first/next history.

## WP-5U12C — Blocks page

Use the complete Fast Index even without X/Y Lock. Show stream summary, Block
type/BFINAL, compressed and output ranges, size, IDAT spans and available
Stored/Dynamic metadata. Current and Manual Selection use text/shape in addition
to color. Actions are exactly `Show in Hex`, `Show inflated output` and
`Open Decode Trace` with typed targets.

## WP-5U12D — Huffman page

For Dynamic blocks show code-length, literal/length and distance construction
in order. For Fixed show predefined tables; Stored shows an explicit no-Huffman
state. Main columns are Symbol, Meaning, Bits, Canonical, Read order and Uses in
current trace. Hide zero-bit entries by default. Canonical and read-order values
are fixed-width binary strings produced by Qt-free projection code.

## WP-5U12E — Decode Trace page

Show bounded scope in the title. Rows distinguish Literal, Match and EOB and
include input bits, output range and semantic event. Match details include
length/distance base and extra bits, source/target, overlap and selected-byte
offset. `Show in Hex` means compressed input; `Show inflated output` means
output. The two operations must never share an untyped integer payload.

## WP-5U12F — Product gate

Verify 320/360/480/600-pixel inspector widths, Light/Dark, keyboard navigation,
copy, accessibility names, virtualized scrolling, loading/empty/partial/error,
large trace response and memory. Table rows use models, not one QWidget per
event. Page switches, row selection, resize and DEC/HEX enqueue zero replays.

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'block_inspector|huffman_inspector|decode_trace|compression_inspector|trace_pipeline' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev
git diff --check
```

## Completion definition

All requirements in normative WP-5U12 section 19 and this document must pass.
Any missing offset origin, truncated physical range, falsely complete bounded
count, unbounded retention or unresolved UI state is `FAIL`, not a known
limitation. Architecture/dependency conflicts are `BLOCKED`.
