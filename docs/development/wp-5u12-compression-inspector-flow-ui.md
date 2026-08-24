# WP-5U12 — Compression Inspector Flow UI Refinement

Status: **implemented (2026-08-24)**  
Milestone: M5 UI Refinement  
Depends on: **WP-5U13** (app wiring of the bounded trace pipeline), WP-5T0A,
WP-5T0B, WP-505A, WP-505B, WP-505C, M5 Trace Gate, WP-5U7, WP-5U10  
Coordinates with: WP-5U9, WP-5U11  
Development plan: `wp-5u12-compression-inspector-development-plan.md`  
Product reference: user-provided PNG Analyzer screenshot dated 2026-08-23

## 1. Authority and precedence

This Work Package is a **presentation-only** refinement of the bounded
Compression Inspector pages. It consumes a live, generation-coherent bundle
published by the application. The pipeline wiring (orchestrator lifecycle,
committed-pixel → inflated-interval mapping, bounded request submission,
worker-thread bridge, `TraceInspectorBinding` publication and Hex/DEFLATE
navigation wiring) is owned by **WP-5U13** and must be landed first. This WP
must not add, remove or resubmit any trace request.

It does not replace the architecture or data contracts established by:

- `AGENTS.md` and `REPOSITORY_LAYOUT.md`;
- ADR-0003 Qt-free core;
- ADR-0004 unified analysis and selection model;
- ADR-0005 Virtual IDAT stream without full concatenation;
- ADR-0006 Fast Index plus on-demand Deep Trace;
- WP-5T0A bounded `TraceQueryResult`;
- WP-5T0B on-demand `TraceOrchestrator`;
- WP-505A/B/C Inspector projections;
- M5 Trace Gate bounded inspector binding; and
- WP-5U13 application integration (which this WP consumes).

If this document appears to require an unbounded whole-file token trace, a
second Inflate path in the GUI, IDAT concatenation, Qt under `libs/`, or
re-introducing trace submission into presentation code, that interpretation is
invalid. Stop and report the conflict rather than weakening the accepted
architecture.

Section 14 is the normative UI contract. It constrains visual implementation
while allowing normal platform-native Qt differences.

## 2. Background

WP-5U13 wires the application so the three pages render a real, bounded
result. Before WP-5U13, the pages always showed the sparse initial state:

```text
Block trace: no trace
scanline: — | current output: —
```

followed by a large empty area. The current pages also render as sparse status
labels followed by wide flat tables. The user-visible problems are:

1. `no trace` does not distinguish no file, no committed selection, replay in
   progress, partial result, cancellation or error.
2. The three pages do not visibly form a drill-down flow.
3. Dense fields are placed in one wide row rather than a compact master table
   with a readable detail area.
4. Current-output context and manual row selection are visually conflated.
5. Stored, Fixed and Dynamic blocks do not have natural page-specific states.
6. Long physical spans, Huffman provenance and Match arithmetic force
   horizontal scrolling even though they are detail information.
7. Existing navigation actions are not consistently explained by the selected
   object and bounded query state.

This WP fixes the presentation of a pipeline that already delivers data; it
does not create the pipeline.

## 3. User-visible outcome

The three pages must read as one bounded investigation flow:

```text
explicit locked pixel/output selection
        ↓ bounded Deep Trace request (owned by WP-5U13)
Associated DEFLATE Blocks
        ↓ select an associated block
Huffman tables for the bounded result
        ↓ inspect the selected/current entry
Decode Trace tokens
        ↓ select literal, match or EOB
Show in Hex / Show in DEFLATE
```

Each page answers one question:

- **DEFLATE Blocks** — which associated block or blocks produced the selected
  output range?
- **Huffman** — which Stored/Fixed/Dynamic table facts are available for those
  associated blocks, and which entry relates to the selected token?
- **Decode Trace** — how did the bounded token replay produce the selected
  output, including Literal, Match and EOB paths?

The UI must remain useful without a current pixel. It may show fast/indexed
structure already available, but it must not automatically generate or retain
an unbounded whole-file token trace.

## 4. Scope

### 4.1 Included

- Replace ambiguous status text with the stable bounded trace states:
  `not indexed`, `replaying`, `ready`, `partial`, `error`, `cancelled`.
- Add one compact shared context area for all three Compression pages.
- Restructure each page into a master table plus selected-object details.
- Preserve and naturally present the existing WP-505A/B/C facts.
- Preserve explicit bounded Deep Trace requests, budgets, cancellation and
  generation checks (submitted by WP-5U13; this WP adds none).
- Preserve `Show in Hex` and `Show in DEFLATE` navigation contracts (wired by
  WP-5U13; this WP only enables/disables and labels them).
- Preserve current scanline/output/token context across page switches.
- Make Current context and manual Selection independently visible.
- Define Stored, Fixed, Dynamic, partial, error and no-selection states.
- Define 320/360/480/600 logical-pixel responsive behavior.
- Add or update focused presentation and GUI tests.

### 4.2 Explicit non-goals

- No new DEFLATE decoder or second decode path.
- No default whole-file token trace or event retention.
- No IDAT payload concatenation.
- No change to `TraceOrchestrator` scheduling, replay policy or budgets.
- No change to Deflate bit parsing, Huffman construction or Match decoding.
- No new trace request submission from any presentation action.
- No new header-event timeline beyond the facts already present in the bounded
  Block/Huffman/Decode Trace projections.
- No automatic recursive Trace-to-Original-Literal expansion.
- No Huffman tree animation, heat map, compression dashboard or statistics UI.
- No changes to Reconstruction, Pixels, Filtered or Defiltered content.
- No changes to the WP-5U11 Hex source order or meaning.
- No new dependencies or external corpus files.

## 5. Allowed and forbidden paths

### 5.1 Primary allowed paths

- `docs/development/wp-5u12-compression-inspector-flow-ui.md`
- `docs/development/wp-5u12-compression-inspector-development-plan.md`
- `ui/qt/include/pnga/ui/qt/block_inspector.h`
- `ui/qt/src/block_inspector.cpp`
- `ui/qt/include/pnga/ui/qt/huffman_inspector.h`
- `ui/qt/src/huffman_inspector.cpp`
- `ui/qt/include/pnga/ui/qt/decode_trace_inspector.h`
- `ui/qt/src/decode_trace_inspector.cpp`
- focused reusable UI helpers under the existing `ui/qt/` canonical tree
- `ui/qt/CMakeLists.txt`
- `ui/qt/README.md`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp` (limited to composing the shared
  context into the Compression tab stack and preserving workspace restore of
  the Compression subpage; **no trace request submission, no navigation wiring
  changes**)
- focused tests under `tests/gui/`
- affected GUI test registration only

### 5.2 Conditional allowed paths

The existing Qt-free Inspector projections may receive a minimal extension only
when a required presentation fact already exists in `TraceQueryResult` but is
not projected. In that case the Work Package may update:

- the owning projection header/source under `libs/analysis-engine/`;
- focused projection unit tests; and
- deterministic serialization only if the owning contract explicitly requires
  it.

The extension must not add decoder behavior, broaden the replay region, remove
budgets or create a whole-file event list. Public contract changes that exceed a
view projection require a separate approved Work Package.

### 5.3 Forbidden paths

- `libs/deflate-trace/**`
- `libs/deflate-runtime/**`
- `libs/deflate-index/**`
- `libs/png-format/**`
- `libs/png-reconstruction/**`
- `third_party/**`
- corpus binaries or manifest changes
- packaging, branding and release files
- unrelated architecture or Work Package documents (including `wp-5u13-*`)

## 6. Existing data boundary

The implementation consumes one immutable, budgeted `TraceQueryResult` for the
current document generation, published through `TraceInspectorBinding` by
WP-5U13. The result is for one explicit inflated-output interval and may
contain:

- associated DEFLATE blocks;
- bounded Literal, Match and EOB tokens;
- Stored/Fixed/Dynamic table summaries;
- logical Deflate input ranges;
- inflated output ranges;
- physical IDAT provenance spans;
- selected output/token context; and
- ready/partial/error/cancelled state.

The GUI must not infer omitted data. Examples:

- A bounded result does not imply a full stream token count.
- An entry present in a table does not imply a whole-block occurrence count.
- Missing scanline/channel mapping must display `—` or a clear unavailable
  explanation, not a guessed coordinate.
- `partial` means verified rows remain valid; it does not mean clear all tables.

## 7. Shared Compression context

The current per-page status and context labels become one shared, compact area
above the page stack. It contains two lines.

### 7.1 Status line

Ready example:

```text
Trace ready · generation 42 · 3 associated blocks · 96 tokens in result
```

Replaying example:

```text
Replaying selected output range… · generation 42
```

Partial example:

```text
Partial trace · 128-token budget reached · verified rows remain available
```

Rules:

- **State source**: the status line is driven by the `TraceInspectorLifecycle`
  value published by `TraceInspectorBinding::publishState` (which carries the
  full stable vocabulary including `not indexed`, `replaying`, `ready`,
  `partial`, `error`, `cancelled`). The per-page projection statuses remain a
  secondary, page-local detail for partial/error text; they must not become
  the shared status source because they cannot express replaying or cancelled.
- Use the stable state vocabulary; do not expose `no trace`.
- Counts describe the bounded result, not the whole file, unless explicitly
  labelled as index/stream counts from an existing fast artifact.
- Generation may remain in copyable diagnostic detail rather than the primary
  sentence if product density requires it.
- Partial/error details include a concise user explanation and preserve rows.

### 7.2 Current mapping line

Ready example:

```text
Current · scanline 16 · output byte 1,573 · Block #2 · Token #35
```

No committed coordinate:

```text
Select and lock a pixel to inspect its bounded DEFLATE provenance.
```

Rules:

- Hover-only coordinates do not trigger Deep Trace.
- The line reflects the committed Selection and Lock semantics owned by the
  main application and published by WP-5U13.
- Switching among the three pages does not resubmit the request.
- Manual table selection may differ from Current; both remain visible.

## 8. DEFLATE Blocks page

### 8.1 Question answered

> Which associated block or blocks produced the selected output range?

The page does not claim to be an unbounded whole-stream token browser.

### 8.2 Layout

```text
┌─ Associated blocks ──────────────────────────────────────────┐
│ C  #  Type      Final  Input bits       Output bytes         │
│    1  Fixed     no     408..785         512..1536            │
│ ●  2  Dynamic   yes    786..1206        1536..3104           │
├─ Block #2 details ───────────────────────────────────────────┤
│ Type          Dynamic Huffman · BTYPE=10                     │
│ Final         yes · BFINAL=1                                 │
│ Input         DEFLATE bits [786, 1206)                       │
│ Output        Inflated bytes [1536, 3104)                    │
│ Current       output byte 1573 · position +37                │
│ IDAT spans    file[67..121), file[133..151)                  │
└──────────────────────────────────────────────────────────────┘
```

### 8.3 Master table

Default columns:

```text
Current | # | Type | Final | Input bits | Output bytes
```

The existing IDAT spans and scanline fields move to the detail area because
they are long or contextual. The scanline is supplied by the caller (WP-5U13)
as a single optional value in `BlockInspectorView.scanline`; the detail shows
that exact value when present, and `—` with a short explanation otherwise. The
projection cannot currently express more than one associated scanline, so the
UI must **not** fabricate a range or a `mixed` label; multi-scanline
expression would require a separately approved projection extension and is out
of scope.

### 8.4 Details

Show only facts in `BlockInspectorView` or a permitted minimal projection
extension:

- block index;
- Stored/Fixed/Dynamic type;
- BFINAL;
- half-open input bit range;
- half-open output byte range;
- selected/current output position;
- physical IDAT provenance spans; and
- scanline context when exact.

Stored details may show LEN/NLEN only if already present in the bounded Huffman
projection; do not duplicate or recompute the values inside the widget.

### 8.5 Interaction

- Single click changes manual Selection and details.
- Current rows carry a dedicated Current marker independent of row selection.
- Page switch preserves the selected associated block where the projections
  share a stable block index.
- `Show in Hex` and `Show in DEFLATE` retain the WP-505A signal semantics wired
  by WP-5U13 and enable only when the required range is available.
- A multi-span physical mapping must not silently pretend the first span covers
  the complete logical range. The bounded action is labelled accurately and
  multi-span navigation is left to a separately approved enhancement.

## 9. Huffman page

### 9.1 Question answered

> Which Huffman/Stored table facts are available for the associated blocks, and
> which entry relates to the selected token?

### 9.2 Layout

```text
┌─ Block #2 · Dynamic ─────────────────────────────────────────┐
│ Table  Code length | Literal / Length | Distance             │
├──────────────────────────────────────────────────────────────┤
│ Build  Symbol  Bits  Canonical       Definition bits         │
│ 1      65      4     6 (4 bits)      812..816                │
│ 1      268     6     53 (6 bits)     816..822  ← selected    │
├─ Symbol 268 details ─────────────────────────────────────────┤
│ Table         Literal / Length                               │
│ Canonical     53 · 6 bits                                    │
│ Provenance    DEFLATE bits [816, 822)                        │
│ Relation      entry associated with selected token           │
└──────────────────────────────────────────────────────────────┘
```

### 9.3 Table scope selector

Use one compact page-local selector:

```text
Code length | Literal / Length | Distance
```

It filters the tables already contained in the bounded result
(`HuffmanInspectorTable.kind`; Stored has no kind and maps to LEN/NLEN). It
does not request a larger replay or create a fourth Inspector level.

### 9.4 Mode behavior

- **Stored**: show LEN/NLEN capability/details from the projection and the
  explanation `Stored blocks do not use Huffman coding.`
- **Fixed**: show the predefined literal/length and distance table capacities
  and any entries provided by the bounded projection. Do not reconstruct the
  complete table in Qt.
- **Dynamic**: preserve build order, table kind, symbol, bit length, canonical
  code and code-length provenance.

### 9.5 Code labelling

- The existing value is labelled `Canonical`; do not use the ambiguous column
  title `Code`.
- Actual stream-read bit text may be shown only when supplied by an approved
  Qt-free projection. Qt must not reverse or reinterpret codes to fabricate a
  `read order` column.
- Provenance identifies where a dynamic table definition was read; it is not a
  token occurrence range.
- Whole-block `Uses` or occurrence counts are not shown unless the bounded
  result explicitly provides their scope. Do not infer full-block counts from a
  truncated result.

### 9.6 Interaction

- The entry associated with the selected token receives a Current marker.
- Manual row selection updates Symbol details without changing the committed
  output Selection.
- No `Show in Hex` action is enabled merely because a canonical entry exists;
  navigation requires a real provenance span or token occurrence supplied by
  the projection.

## 10. Decode Trace page

### 10.1 Question answered

> How did the bounded token replay produce the selected output?

### 10.2 Layout

```text
┌─ Tokens in bounded result ───────────────────────────────────┐
│ C  Token  Path      Input bits    Output bytes               │
│    34     Literal   918..922      1568..1569                 │
│ ●  35     Match     922..937      1569..1587                 │
│    36     EOB       937..944      1587..1587                 │
├─ Token #35 · Match ──────────────────────────────────────────┤
│ Huffman      symbol 268                                     │
│ Length       18 = base 17 + extra 1 (1 bit)                 │
│ Distance     7 = base 7 + extra 0 (0 bits)                  │
│ Source       [1562, 1580) · overlap-safe root origins       │
│ Output       [1569, 1587)                                   │
│ Current      output byte 1573                               │
└──────────────────────────────────────────────────────────────┘
```

### 10.3 Master table

Default columns:

```text
Current | Token | Path | Input bits | Output bytes
```

Huffman symbol, Match arithmetic, source intervals and selected output details
move to the detail area. This removes the current nine-column horizontal wall
without discarding information.

### 10.4 Details by path

#### Literal

- token index;
- input bit range;
- output byte range;
- Huffman symbol when provided;
- selected output byte when associated.

#### Match

- Huffman symbol;
- decoded length with base, extra value and extra-bit count;
- decoded distance with base, extra value and extra-bit count;
- output range;
- overlap-safe match source ranges/root origins from the projection; and
- selected output byte.

#### End of block

- token index;
- input bit range; and
- explicit `End of block` explanation.

The UI must not recursively expand every Match. The existing bounded,
user-triggered Trace-to-Original-Literal behavior remains separate.

### 10.5 Interaction

- Current token/output and manual row selection remain independent.
- `Show in Hex` and `Show in DEFLATE` use existing WP-505C ranges and the
  WP-5U13 wiring.
- Buttons disable with an accessible reason when the range is unavailable.
- Switching to Huffman retains the associated block/token context supported by
  the shared bounded bundle; it does not submit a new replay.

## 11. State model and copy

### 11.1 No file

```text
Open a PNG to inspect its compressed IDAT stream.
```

No empty table is shown.

### 11.2 File open, no committed output selection

```text
Select and lock a pixel to inspect its bounded DEFLATE provenance.
```

Fast/indexed structure may remain visible if already available through the
existing application model. No Deep Trace starts from hover.

### 11.3 Not indexed

```text
DEFLATE provenance is not indexed for this selection.
```

### 11.4 Replaying

```text
Replaying the selected output range…
```

If an older same-generation verified result remains visible, it may stay with a
clear stale/loading indication; a previous document generation may not remain.

### 11.5 Ready

Show tables and details. Do not display generation as the dominant product
message unless diagnostics mode requires it.

### 11.6 Partial

```text
Partial trace · verified rows are shown · <bounded reason>
```

Keep verified blocks/tables/tokens navigable.

### 11.7 Error

```text
Trace stopped at <bounded location>: <user-facing reason>
```

Keep verified rows that precede the error. Internal exception or enum text may
appear only in copyable diagnostic details.

### 11.8 Cancelled

Cancellation from a superseded selection normally publishes no stale result.
If a user-visible cancellation state is deliberately retained, show:

```text
Trace request cancelled.
```

Do not convert cancellation into `error`.

## 12. Selection, generation and navigation

### 12.1 Selection rules

- The application-owned Selection remains authoritative.
- A committed X/Y selection maps to an inflated-output interval through the
  existing reconstruction/stage model; WP-5U13 owns the mapping and request.
- One pixel may map to multiple source bytes/tokens/blocks; the UI must preserve
  range and plurality rather than choosing a fabricated one-to-one mapping.
- Current markers come from the bounded Inspector bundle.
- Manual selection is local browsing state and must not overwrite dimensions of
  the shared Selection that the page does not own.

### 12.2 Trigger rules

- Explicit image click, committed X/Y change, or an existing explicit
  provenance command may request bounded Deep Trace (submitted by WP-5U13).
- Hover never requests Deep Trace.
- Changing Compression subpage never requests Deep Trace.
- Changing DEC/HEX never requests Deep Trace.
- Selecting a table row never broadens the replay region.
- This WP adds no new request triggers; presentation actions must be verified
  to submit zero requests.

### 12.3 Generation rules

- All three pages publish from the same bundle generation.
- New document generation clears or replaces old visible results atomically.
- Stale/cancelled worker callbacks cannot update status, Current markers,
  details or navigation buttons.
- The UI thread only receives immutable projections.

### 12.4 Navigation rules

- Preserve existing WP-505A/C signal units and half-open ranges (wired by
  WP-5U13).
- Preserve WP-5U11 `File | IDAT | Inflated | Defiltered` source meanings.
- A physical file offset must never be applied as an IDAT or Inflated offset.
- A logical Deflate bit range must never be displayed as a file byte range.
- Multi-span navigation must state its supported scope; it may not widen across
  PNG Chunk headers or CRC fields.

## 13. Performance and implementation constraints

- Bounded `TraceQueryResult.max_tokens` remains the primary row bound.
- Do not request a larger budget merely to fill visual space.
- Reuse the existing Qt-free projections and bundle publication.
- Moving fields from a wide master table into a details panel must not duplicate
  large payloads.
- Format long span and arithmetic text on selection or visible-row update.
- Existing `QTableWidget` may be retained if bounded-size tests and GUI gates
  pass. Migration to `QAbstractTableModel` is not required by this WP and must
  not become an unrelated refactor.
- Page switching, manual row selection and resizing perform no file reads,
  replay or decode.
- Do not create one QWidget per token beyond the existing bounded table items.
- Do not use a large minimum width to avoid responsive design.

## 14. Normative UI contract

### 14.1 Fixed component hierarchy

```text
CompressionInspector
├─ CompressionSubTabs
│  ├─ DEFLATE Blocks
│  ├─ Huffman
│  └─ Decode Trace
├─ SharedTraceContext
│  ├─ TraceStatusLine
│  └─ CurrentMappingLine
├─ CompressionPageStack
│  ├─ BlocksPage: MasterTable + Details
│  ├─ HuffmanPage: TableKindSelector + MasterTable + Details
│  └─ DecodeTracePage: MasterTable + Details
└─ PageNavigationActions when applicable
```

Do not add summary cards, a local DEC/HEX selector, search, a fourth subpage or
a full-page dashboard. Details remain in-page rather than modal-only.

### 14.2 Geometry

Logical-pixel targets; platform theme may differ by up to 2 px without review:

| Element | Target | Allowed range |
|---|---|---:|
| Compression subtab height | 28 | 26–30 |
| Page horizontal margin | 12 | 10–14 |
| Shared context vertical padding | 8 | 6–10 |
| Context line spacing | 4 | 3–6 |
| Master header height | 28 | 26–31 |
| Master row height | 28 | 26–32 |
| Details header height | 30 | 28–34 |
| Master/details splitter hit area | 6 | 5–8 |
| Navigation button height | 28 | 26–32 |

- Use the application UI font and existing theme tokens.
- Use the application monospace token for bit ranges and arithmetic.
- Do not shrink the entire page font to fit columns.
- Master and details default to approximately `55:45` of available page height.
- Each area scrolls internally; do not create a nested full-page scroll that
  makes the master header disappear during normal detail reading.

### 14.3 Responsive widths

#### 600 px and wider

- Shared status and mapping normally fit one line each.
- All default master columns are visible.
- Details use label/value rows.

#### 420–599 px

- Context lines may wrap once.
- Default master columns remain; long ranges elide with tooltip or scroll only
  inside the table.
- Details remain label/value rows.

#### 360–419 px

- Subtabs use Qt scroll buttons rather than indistinguishable ellipsized text.
- Blocks retain Current/#/Type/Final/Input/Output.
- Huffman retains Build/Symbol/Bits/Canonical; Mode/Table moves to the section
  heading and details; Provenance moves to details.
- Trace retains Current/Token/Path/Input/Output.
- Navigation labels remain complete.

#### 320–359 px

- Context and details may stack labels above values.
- Master table may scroll horizontally inside its viewport.
- Navigation buttons may stack vertically while retaining order.
- The Inspector minimum width must not increase above the established layout
  contract.

Below 320 px the page may require table scrolling, but it must not crash,
overlap sibling docks or create an unbounded minimum width.

### 14.4 Current and manual Selection

- Current uses a dedicated marker column with `●` or the existing semantic
  Current icon and accessible text.
- Manual Selection uses the platform-native selected row and focus ring.
- Current plus Selection retains both signals; never replace both with a third
  saturated row color.
- Current is not cleared merely because the user browses another row.
- Selection does not automatically rewrite the shared X/Y Selection.

### 14.5 Details behavior

- Details heading names the selected object: `Block #2`, `Symbol 268`,
  `Token #35 · Match`.
- No manual row selection: details follow Current when one exists.
- Neither manual nor Current: show one short instruction, not placeholder
  labels for every field.
- Long physical spans wrap and remain copyable.
- Empty/unavailable facts display `—` plus an explanation when ambiguity would
  otherwise remain.

### 14.6 Locked visible labels

```text
DEFLATE Blocks
Huffman
Decode Trace
Associated blocks
Code length
Literal / Length
Distance
Canonical
Input bits
Output bytes
Show in Hex
Show in DEFLATE
```

The application-wide localization policy may translate labels consistently.
Do not reintroduce `Block trace: no trace`, `Huffman trace: no trace` or
`Decode trace: no trace`.

### 14.7 Visual baseline matrix

Before real binding changes, use deterministic existing projection fixtures to
capture:

```text
Blocks: 360 / 480 / 600 px, light
Huffman: 360 / 480 / 600 px, light
Decode Trace: 360 / 480 / 600 px, light
All three: 360 / 480 px, dark
Stored Huffman: 360 / 480 px
Replaying, Partial and Error: 360 / 480 px
Current + Selection: Blocks and Decode Trace
```

Screenshot comparison may tolerate native font antialiasing, scrollbar and
1–2 px platform border differences. It may not hide:

- clipped tabs, headers, buttons or detail text;
- changed hierarchy or column order;
- missing Current or Selection state;
- controls overlapping at 360/480 px;
- a content-driven Inspector minimum-width increase;
- blank tables with ambiguous status;
- verified rows disappearing in partial/error state; or
- duplicate navigation controls.

## 15. Accessibility and keyboard

- Reuse native tab and table keyboard behavior.
- Provide accessible names for shared status, Current mapping, each master
  table, details area and navigation button.
- Announce state changes without repeatedly stealing focus.
- Current, Selection, Partial and Error are not color-only.
- Focus remains visible on Current rows.
- Table rows and details are copyable.
- Long text exposes the complete value through selection, tooltip or accessible
  description.

## 16. Tests

### 16.1 Qt-free projection tests

Only when a permitted projection changes, cover:

- Current and manual selected object remain distinguishable;
- half-open bit/output ranges pass through unchanged;
- Stored/Fixed/Dynamic table mode/build order remains deterministic;
- partial/error rows are retained;
- missing optional fields remain missing rather than guessed; and
- generation is preserved.

### 16.2 Component GUI tests

- No file shows the no-file state and no empty table.
- No committed selection shows the explicit bounded-trace instruction.
- Replaying/ready/partial/error/cancelled text maps to the stable state (the
  shared status line reads from the published `TraceInspectorLifecycle`).
- Blocks master/detail renders Stored/Fixed/Dynamic facts.
- Huffman Stored, Fixed and Dynamic states render naturally.
- Huffman labels canonical values unambiguously.
- Decode Trace Literal, Match and EOB details are complete.
- Match shows base, extra value/count, decoded length/distance and source
  intervals from the projection.
- Current and manual Selection coexist.
- Navigation enablement follows selected-object ranges.
- DEC/HEX formatting changes no domain range or replay generation.

### 16.3 Main-window integration tests

- `Reconstruction | Compression` hierarchy remains unchanged.
- Compression subtab switching preserves bounded bundle generation.
- Explicit committed coordinate triggers at most one bounded replay request
  (submission owned and tested by WP-5U13; this WP verifies it adds none).
- Hover and subtab switching trigger no replay.
- Stale/cancelled generations do not update any of the three pages.
- WP-5U11 Hex source meanings and navigation remain correct.
- Workspace restore preserves the Compression group and subpage.
- Selecting non-IDAT chunks does not corrupt the current Compression result.

### 16.4 Responsive and GUI gate

- Test 320, 360, 480 and 600 px Inspector body widths.
- Test default, 150% and 200% Qt scale paths already covered by the GUI gate.
- Test light/dark theme where the current platform supports deterministic
  switching.
- Run truncation sentinels for tab labels, table headers and buttons.
- Verify focus order and accessible names.

### 16.5 Regression

- Existing WP-505A/B/C projection and Qt tests continue to pass.
- WP-5T0A/B, M5 Trace Gate and WP-5U13 integration tests continue to pass
  unchanged.
- Existing WP-5U6A/B/C failure, performance and cross-platform GUI gates remain
  green.
- No new file I/O, decode, replay or allocation appears on page switch/resize.

## 17. Implementation slices

1. **P0-A — Baseline audit**
   - Confirm WP-5U13 is landed: the three pages receive a live bundle, and
     navigation signals are wired. If not landed, report `BLOCKED`; do not
     re-implement wiring.
   - Confirm current widgets, projections, state mapping and navigation units.
2. **P0-B — Shared context and static page skeleton**
   - Build the normative hierarchy with deterministic existing fixtures.
3. **P0-C — Blocks master/detail refinement**
   - Preserve WP-505A facts and navigation.
4. **P0-D — Huffman master/detail refinement**
   - Preserve Stored/Fixed/Dynamic build facts and selected-entry mapping.
5. **P0-E — Decode Trace master/detail refinement**
   - Preserve Literal/Match/EOB and overlap-safe source facts.
6. **P0-F — State and bounded binding presentation**
   - Drive the shared status line from the published `TraceInspectorLifecycle`;
     remove the ambiguous `no trace` strings; preserve generation and add no
     replay.
7. **P1-G — Responsive, theme and accessibility gate**
8. **P1-H — Full regression and evidence**

P0-B must pass visual baseline review before the three pages are individually
rewired. No slice may broaden Deep Trace scope to make sample content easier.

## 18. Verification commands

At minimum, run the repository-standard checks plus focused tests identified by
the implementation audit:

```bash
python3 scripts/verify_repository_layout.py
cmake --preset dev
cmake --build --preset dev -j
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
```

If the local host cannot provide a required native platform/theme combination,
record it as release evidence still needed; do not claim it passed.

## 19. Deliverables

- Shared Compression context and three master/detail pages consuming the
  WP-5U13 wired bundle.
- Updated focused Qt-free projections only if the D0 audit proves necessary.
- Component and main-window GUI tests (request-count assertions inherited from
  the WP-5U13 baseline).
- Updated responsive/DPI/accessibility evidence.
- Before/after screenshots for the Section 14.7 matrix.
- Audit note explaining how bounded on-demand trace behavior was preserved and
  that no presentation action submits a request.
- Verification commands and results.
- Explicit list of any approved UI deviations.

## 20. Definition of done

WP-5U12 is complete only when:

1. The three pages clearly answer Blocks, Huffman and bounded Decode Trace
   questions without duplicating one wide debug table.
2. No page displays an unexplained `no trace` state.
3. No file, no selection, not indexed, replaying, ready, partial, error and
   cancelled states are distinct and natural; the shared status line reads the
   published `TraceInspectorLifecycle`.
4. Existing WP-505A/B/C facts remain present and correct.
5. Stored, Fixed and Dynamic modes render correctly without GUI recomputation.
6. Literal, Match and EOB detail is preserved; Match arithmetic and overlap-safe
   source ranges remain exact.
7. Current and manual Selection are simultaneously visible and accessible.
8. Page switching, resizing and row selection do not request Deep Trace.
9. Explicit trace requests remain bounded, cancelable and generation-safe
   (submitted only by WP-5U13; this WP adds none).
10. No whole-file token trace is generated or retained by default.
11. Virtual IDAT spans remain segmented; no payload concatenation is added.
12. Existing Hex/DEFLATE navigation units and WP-5U11 source semantics remain
    correct.
13. The UI passes the 320/360/480/600 px, DPI, theme, truncation and
    accessibility gates without increasing Inspector minimum width.
14. Partial/error results retain verified rows.
15. Existing M5 trace, WP-505, WP-5U13, UI failure/performance/cross-platform
    tests and repository layout checks pass.

## 21. Implementation evidence (2026-08-24)

- Shared `CompressionContext` (status + mapping lines) above the page stack,
  driven by `TraceInspectorBinding` from `TraceInspectorLifecycle`; the per-page
  `no trace` labels were removed.
- `CompressionInspectorPage` master/detail shell; the three pages become
  master table + details:
  - Blocks: `Current | # | Type | Final | Input bits | Output bytes`; details
    show type, BFINAL, half-open ranges, current output, IDAT spans, scanline.
  - Huffman: page-local `Code length | Literal / Length | Distance` selector
    filtering the published bounded tables; `Current | Build | Symbol | Bits |
    Canonical | Definition bits`; Stored shows the no-Huffman explanation;
    Canonical labelling preserved.
  - Decode Trace: `Current | Token | Path | Input bits | Output bytes`; details
    per Literal / Match (base+extra length/distance, overlap-safe sources) /
    EOB.
- Current uses a dedicated `●` marker column, independent of native manual row
  selection.
- Responsive shell: master tables scroll horizontally inside the viewport and
  never raise the Inspector minimum width (320/360/480/600 px tested).

Verification: full `dev` suite 39/39 (incl. new
`gui_compression_inspector_responsive_tests`), GUI gate PASS
(`build/gui-gate/wp-5u6c-evidence.json`), repository layout 0/0, dependency
audit 0/0. No Qt-free projection, decoder, budget or Virtual IDAT behavior
changed; no presentation action submits a trace request.
