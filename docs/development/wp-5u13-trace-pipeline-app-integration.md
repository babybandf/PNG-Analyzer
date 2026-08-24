# WP-5U13 — Bounded Trace Pipeline Application Integration

Status: **implemented (2026-08-24)**  
Milestone: M5 UI Refinement
Depends on: WP-5T0A, WP-5T0B, WP-505A, WP-505B, WP-505C, M5 Trace Gate,
WP-5U11
Precedes: WP-5U12 (presentation refinement consumes the wired pipeline)
Coordinates with: WP-5U12

## 1. Why this Work Package exists

The Qt-free bounded trace components and their Qt boundary are implemented and
tested, but the desktop application never connects them. `MainWindow` creates
the three Compression inspector widgets (`BlockInspector`, `HuffmanInspector`,
`DecodeTraceInspector`) inside the `Compression` tab stack, yet never:

- creates a `TraceOrchestrator`;
- creates a `TraceInspectorBinding`;
- submits a `TraceOrchestrationRequest`;
- publishes a `TraceQueryResult` / `TraceInspectorBundle` to the pages; or
- connects the pages' `Show in Hex` / `Show in DEFLATE` signals.

As a result the three pages always render their `no trace` initial state at
runtime. The WP-5U12 draft was scoped as a presentation refinement of an
"already wired" pipeline; the D0 audit proved the wiring is the missing piece.
This Work Package delivers that wiring first so WP-5U12 can refine a pipeline
that actually produces data.

## 2. Goal and non-goals

### 2.1 Goal

Connect the existing components end to end in the application:

- One `TraceOrchestrator` per document generation that maps a committed,
  locked pixel to a bounded inflated-output interval and submits a bounded
  Deep Trace request on explicit commit.
- A worker-thread result bridge that publishes one generation-coherent
  `TraceInspectorBundle` through `TraceInspectorBinding` to the three existing
  pages, and publishes `TraceInspectorLifecycle` states (not indexed,
  replaying, ready, partial, error, cancelled).
- `Show in Hex` and `Show in DEFLATE` navigation from Blocks and Decode Trace
  wired to `HexView` with the WP-5U11 source semantics.
- Integration tests proving real data appears, request counts are bounded,
  stale generations never publish, and navigation units are never interchanged.

### 2.2 Non-goals

- No presentation restructuring (master/detail, shared context, responsive,
  accessibility) — that is WP-5U12, which consumes the wired bundle.
- No decoder, Inflate, index, reconstruction or Virtual IDAT change.
- No change to `TraceOrchestrator` scheduling, replay policy or budgets.
- No IDAT payload concatenation.
- No whole-file token trace or event retention.
- No change to the three widgets' `setView` / signal contracts.
- No new dependencies or corpus assets.

## 3. Allowed and forbidden paths

### 3.1 Primary allowed paths

- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `apps/png-analyzer-gui/CMakeLists.txt` (only to add a new local worker, if
  needed)
- focused tests under `tests/gui/` and affected GUI test registration

### 3.2 Conditional allowed paths

Only if the D0 audit proves a small seam is required to bridge the
`TraceOrchestrator` worker thread onto the UI thread:

- a focused reusable helper under `ui/qt/` (e.g. a `TraceQueryStatusBridge`
  mirroring `QueryStatusBridge`), plus its GUI test.

The three inspector widgets must remain unchanged in this WP.

### 3.3 Forbidden paths

- `libs/analysis-engine/**` (projections, bundle, state machine and
  orchestrator already exist and are tested; no change is authorized here)
- `libs/deflate-*/**`, `libs/png-format/**`, `libs/png-reconstruction/**`,
  `libs/backend-libpng/**`
- `third_party/**`, corpus binaries or manifest changes
- packaging, branding and release files
- `docs/development/wp-5u12-*` (owned by WP-5U12)

## 4. Existing data boundary

The application already owns the pieces this WP composes:

| Component | Location | State |
|---|---|---|
| `TraceQueryResult` / `compose_trace_query` | `libs/analysis-engine` | implemented, unit-tested |
| `TraceOrchestrator` | `libs/analysis-engine` | implemented, unit-tested |
| `TraceInspectorStateMachine` | `libs/analysis-engine` | implemented, unit-tested |
| `TraceInspectorBinding` | `ui/qt` | implemented, GUI-tested |
| `Block/Huffman/DecodeTraceInspector` widgets | `ui/qt` | implemented, GUI-tested |
| `QueryCoordinator` scanline/stream-row mapping | `libs/analysis-engine` + app | wired for Reconstruct only |

The committed coordinate is already published on the `SelectionBus` with a
document generation. The existing `QueryCoordinator` path
(`stream_row_for_pixel` + scanline anchors, used at
`apps/png-analyzer-gui/src/main_window.cpp:1338`) is the precedent for
mapping an image coordinate to a stream row; the same geometry must be reused
to derive the inflated-output interval for the trace request, not
reimplemented in this WP.

## 5. Integration design

### 5.1 Orchestrator lifecycle

- Create one `TraceOrchestrator` per document in `MainWindow`, following the
  existing `QueryCoordinator` ownership pattern
  (`openQueryCoordinator`). Recreate or reset it on `openFile` so an older
  document's coordinator cannot publish into a newer generation.
- Set the result callback before the first submit; bridge worker-thread
  callbacks to the UI thread with a queued `QMetaObject::invokeMethod` (or a
  `TraceQueryStatusBridge`-style object) exactly like the existing
  `QueryStatusBridge`.
- Keep `TraceInspectorStateMachine` authoritative for lifecycle and stale
  suppression; feed it from the callback before publishing through
  `TraceInspectorBinding::publishState` / `publish`.

### 5.2 Interval mapping

- On a committed, locked pixel (bus selection with `Stage::kDelivered`), map
  the image coordinate to the stream row with the existing layout helpers,
  then to the inflated-output byte interval for that row using the existing
  scanline-anchor layout.
- Submit a `TraceOrchestrationRequest` with:
  - the current document generation;
  - the committed selection;
  - the derived half-open `inflated_begin` / `inflated_end`;
  - the existing bounded `max_tokens` and trace output budget (do not change
    them); and
  - `JobPriority::kSelection`.
- Deduplicate: a re-commit of the identical interval must not enqueue a second
  replay.

### 5.3 Request triggers

| Action | Bounded replay request |
|---|---:|
| Explicit committed pixel / X-Y Lock | 0 or 1 (deduped per identical interval) |
| Hover | 0 |
| Compression subpage switch | 0 |
| Manual row selection | 0 |
| DEC/HEX switch | 0 |
| Inspector resize | 0 |

### 5.4 Publication

- `TraceInspectorBinding::publish` delivers one generation-coherent bundle to
  all three pages; `publishState` delivers lifecycle text and keeps the last
  verified rows for partial/error.
- Never publish a stale or cancelled result for an older document generation.
- `clear()` / `replaceDocument` on new file, reload and application close.

### 5.5 Navigation wiring

Connect the existing widget signals to `HexView` using WP-5U11 source
semantics. The hex sources are `File | IDAT | Inflated | Defiltered`; there is
no separate "DEFLATE" source, so a logical Deflate bit range is shown in the
IDAT Stream source at `bit_begin / 8` with the bit range stated in the label.

| Signal | Payload | Hex target | Guardrail |
|---|---|---|---|
| Block `showInHexRequested` | first physical file span (offset, length) | `File` source, `navigateTo(offset)`, highlight span | physical offset never applied to IDAT/Inflated |
| Block `showInDeflateRequested` | logical Deflate bit `[begin, end)` | `IDAT` source, `navigateTo(begin/8)` | bit range never shown as file bytes |
| Decode `showInHexRequested` | inflated output byte `[begin, end)` | `Inflated` source, `navigateTo(begin)` | output offset never applied to File |
| Decode `showInDeflateRequested` | logical Deflate bit `[begin, end)` | `IDAT` source, `navigateTo(begin/8)` | bit range never shown as file bytes |

Multi-span provenance keeps the existing single-span emit; the UI must label
the bounded action accurately and must not pretend the first span covers the
complete logical range (handled by WP-5U12 presentation).

## 6. Tests

### 6.1 Main-window integration tests

- Opening a deterministic fixture PNG and committing a pixel makes all three
  pages show real, same-generation data (not the initial `no trace` state).
- Committing the same pixel twice enqueues at most one replay.
- Hover, subpage switch, row selection and resize enqueue zero replays.
- Switching files invalidates older generations; a stale/cancelled callback
  updates no page.
- Block `Show in Hex` navigates the File source at the physical offset; Block
  `Show in DEFLATE` navigates the IDAT source at `begin/8`.
- Decode Trace `Show in Hex` navigates the Inflated source; `Show in DEFLATE`
  navigates the IDAT source.
- Selecting a non-IDAT chunk does not corrupt the current Compression result.
- Workspace restore keeps the Compression group and subpage without a replay.

### 6.2 Projection / component regression

- Existing WP-505A/B/C, WP-5T0A/B, M5 Trace Gate and GUI gate tests continue
  to pass unchanged (this WP adds wiring only).

## 7. Verification commands

```bash
python3 scripts/verify_repository_layout.py
cmake --preset dev
cmake --build --preset dev -j
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
```

## 8. Deliverables

- `MainWindow` wiring: `TraceOrchestrator` lifecycle, interval mapping,
  bounded submit, thread bridge, `TraceInspectorBinding` publication and
  generation/cancellation handling.
- `Show in Hex` / `Show in DEFLATE` navigation wiring with unit-preserving
  source mapping.
- Integration tests and request-count evidence.
- Audit note recording the D0 finding (pipeline was unwired) and how the
  bounded on-demand behavior is preserved.

## 9. Definition of done

WP-5U13 is complete only when:

1. A committed, locked pixel produces a bounded trace that all three pages
   render from the same generation.
2. The `no trace` initial text appears only when there is genuinely no data
   (no file, no committed selection, not indexed, replaying, partial, error
   or cancelled), never because wiring is missing.
3. Hover, subpage switch, row selection, DEC/HEX and resize enqueue zero
   replays; identical re-commit enqueues at most one.
4. Stale or cancelled callbacks never update any page or navigation button.
5. `Show in Hex` / `Show in DEFLATE` navigate the correct WP-5U11 source and
   never interchange physical, logical IDAT, Deflate bit or Inflated byte
   units.
6. No decoder, index, reconstruction, projection, budget or Virtual IDAT
   behavior changed.
7. Repository layout, focused integration tests and the full regression pass.

## 10. Stop conditions

Report `BLOCKED` instead of proceeding when:

- mapping a committed pixel to an inflated-output interval requires new
  decoder/reconstruction geometry not present in the existing anchors/layout;
- completing navigation requires changing accepted offset semantics;
- the required wiring would concatenate IDAT payloads;
- the required wiring needs Qt under `libs/` or a new dependency; or
- the required wiring would broaden `TraceOrchestrator` budgets or replay
  scope.

## 11. Implementation evidence (2026-08-24)

Wired in `apps/png-analyzer-gui/src/main_window.*` and verified by
`tests/gui/trace_pipeline_integration_test.cpp`:

- One `TraceOrchestrator` per document opened in `onStageDone` after the scanline
  anchor index; generation set; worker-thread result bridged to the UI thread
  via a queued `QMetaObject::invokeMethod`.
- Committed pixel/X-Y maps through `stream_row_for_pixel` + the anchor
  `scanlines` spans to a half-open inflated-output interval; identical
  intervals are deduplicated and an in-flight task is cancelled before a new
  submit (`kMaxTraceTokens = 4096`, `kTraceOutputBudgetBytes = 4 MiB`).
- `TraceInspectorStateMachine` + `TraceInspectorBinding` publish one
  generation-coherent bundle to all three pages; stale generations never
  publish.
- `Show in Hex` / `Show in DEFLATE` wired per WP-5U11 semantics: File for block
  physical spans, Inflated for output bytes, IDAT for logical Deflate bits
  (block bits absolute; token bits offset by `deflate_data_begin`).
- `main_window.h` stays lean (forward-declared trace types, `~MainWindow` in the
  `.cpp`) so the GUI moc TUs never instantiate the latent
  `TraceQueryResult::operator==` compile issue; no `libs/**` change was needed.

Verification: full `dev` suite 39/39, GUI gate PASS
(`build/gui-gate/wp-5u6c-evidence.json`), repository layout 0/0,
dependency audit 0/0.
