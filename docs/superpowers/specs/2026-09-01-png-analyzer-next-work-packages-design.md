# PNG Analyzer Next Work Packages Design

Status: approved design, pending written-spec review (2026-09-01)

## 1. Purpose

This design converts the remaining product work into independently reviewable
Work Packages. It covers:

1. a behavior-preserving `MainWindow` decomposition;
2. completion of the product-level Compression Inspector;
3. native Windows/macOS theme evidence;
4. native three-platform GUI, accessibility, performance and corpus evidence;
5. Statistics UI plus shared JSON/CSV export for GUI and CLI; and
6. complete first-release APNG parsing, frame decode, canvas composition and
   timeline UI.

Compare and First Difference remain deferred. No package in this plan may add a
Compare entry, placeholder, hidden dual-document state or APNG comparison
semantics.

## 2. Approved delivery strategy

Use a layered, multi-track sequence:

```text
WP-5U15 MainWindow decomposition
  ├─ Compression completion ─┬─ native theme evidence
  │                          └─ cross-platform quality evidence
  ├─ Statistics UI/export
  └─ APNG model → parse → stream → decode → compose → timeline → GUI
                                  ↓
                     final release evidence audit
```

`WP-5U15` is the only common prerequisite. After it passes, the three feature
tracks may proceed independently when their writable paths do not overlap.
Tasks inside one track follow their documented dependency order.

## 3. Global architecture constraints

- Accepted ADRs, `AGENTS.md` and `REPOSITORY_LAYOUT.md` remain authoritative.
- Qt stays under `ui/qt`, `apps/png-analyzer-gui`, GUI tests and approved
  packaging helpers.
- GUI code consumes immutable analysis results; it does not parse PNG, Inflate,
  reconstruct scanlines or derive Huffman facts.
- IDAT and fdAT payloads remain virtual streams backed by source spans. Complete
  compressed payloads must never be concatenated into a new large buffer.
- Fast, generation-level indexes remain separate from bounded, on-demand Deep
  Trace windows.
- All input-derived arithmetic is checked before offset, extent, allocation or
  work-budget use.
- Async publication carries document generation and is discarded when stale.
- Partial, cancelled, budget-exceeded and error results preserve verified
  prefixes and never invent zero-valued facts.
- No new third-party dependency, package manager, top-level source directory or
  reverse dependency edge is authorized.
- Every external fixture requires source, license, SHA-256, expected
  classification and linked tests. Programmatic fixtures are preferred.

## 4. MainWindow decomposition decision

The current `main_window.cpp` has 2,008 lines and its header has 318 lines. It
combines UI construction, QSettings, recent files, document state, four worker
types, Selection/Hex navigation and bounded Trace orchestration.

`WP-5U15` extracts six focused units under `apps/png-analyzer-gui/src`:

| Unit | Responsibility |
|---|---|
| `MainWindowUi` | Construct menus, docks, splitters, tabs and typed widget references |
| `WorkspaceController` | QSettings, layout restore/reset and recent-file state |
| `DocumentWorkers` | Decode, Stage, Validation and Chunk Detail worker types |
| `DocumentSession` | Source/index/results, generation and worker publication |
| `SelectionNavigationController` | X/Y Lock, SelectionBus, Chunk/Pixel/Hex navigation |
| `TraceController` | TraceOrchestrator, lifecycle state, binding and cancellation |

`MainWindow` remains the `QMainWindow` facade and composition root. Its target
size is at most 600 lines of implementation and 160 lines of header; a new
source file should normally remain below 500 lines. These are maintainability
gates, not permission to hide coupled behavior in generated or catch-all files.

The decomposition preserves object names, action text, shortcuts, settings
keys, default/minimum layout, signal payloads, budgets and visible behavior.

## 5. Compression and quality decision

The revised WP-5U12 audit is implemented through six ordered increments:

1. freeze File/zlib/DEFLATE-payload/Inflated offset domains and complete the
   wrapper/IDAT/Adler/full-Block Fast Index projection;
2. separate Current context from Manual Selection and introduce typed,
   generation-safe multi-span navigation;
3. finish the Blocks page;
4. finish the Huffman page;
5. finish the Decode Trace page; and
6. close responsive, accessibility, error-state and performance gates.

Blocks use the complete Fast Index. Huffman and Decode Trace remain bounded
selection results. Navigation always names its offset domain and retains every
physical span; using only the first span to represent a cross-IDAT range is a
correctness failure.

The quality track adds a controlled static PNG/Trace corpus, native theme
screenshots, native GUI/accessibility checks on Windows, macOS and Ubuntu LTS,
Windows/macOS performance baselines, and one audit manifest. `NOT_CONFIGURED`
is evidence of missing capability, never a PASS.

## 6. Statistics decision

The existing bounded Qt-free aggregation engine remains the owner of scalar
statistics. The reactivated feature adds a versioned result contract and a
whole-document streaming token aggregation path. Tokens are aggregated as
they are decoded and discarded; the implementation must not retain a
whole-file token event list.

Each section has independent status, completeness and scope. Missing or
unfinished sections are not rendered as zero. Collection starts lazily when
the Statistics tab is first opened, or explicitly when the CLI statistics
command runs.

The GUI adds a third Inspector top-level tab:

```text
Reconstruction | Compression | Statistics
Statistics: Overview | Chunks | Filters | DEFLATE
```

JSON and CSV share one Qt-free serializer. JSON uses
`schema="pnga.statistics"` and `schema_version=1`. CSV is UTF-8 without BOM,
uses LF and RFC 4180 escaping, and has exactly these columns:

```text
schema_version,section,metric,key,value,unit
```

Status, completeness and scope are represented by standard metric rows. GUI
and CLI output for the same snapshot must be byte-identical.

The CLI command is:

```text
pnga statistics <file> --format json|csv
```

Chunk, Filter and Block navigation uses existing indexes. Token, length and
distance navigation executes an explicit bounded occurrence query; it does not
retain every occurrence during aggregation.

## 7. APNG decision

APNG implements a complete first release without Compare. The normative source
is the W3C PNG Specification, Third Edition, sections 4.9 and 11.3.6.

### 7.1 Image identity

The existing numeric `ImageCoordinate.frame` cannot distinguish an APNG static
fallback from animation frame 0 when the static IDAT image is not part of the
animation. A prerequisite architecture package therefore introduces explicit
identity:

```text
StaticImage
AnimationFrame(index)
```

It updates Selection and Stage serialization and clarifies ADR-0004 and
ADR-0007. A sentinel frame number is forbidden. Legacy static selections must
continue to parse deterministically.

### 7.2 Parse and frame streams

- Parse acTL, fcTL and fdAT through public project code under `png-format`.
- Validate zero-based shared fcTL/fdAT sequence numbers with no gaps or
  duplicates; validate ordering, frame count, geometry, delay, Blend and
  Dispose values.
- Distinguish an IDAT static image included as frame 0 from a separate static
  fallback.
- Build one `VirtualFrameStream` per animation frame. fdAT sequence-number
  bytes are excluded from compressed payload spans.
- Inherit IHDR color type, bit depth, interlace and palette/transparency
  context while using fcTL frame width and height.

### 7.3 Decode and composition

Each selected frame reuses the existing zlib, scanline, filter, native-sample
and delivered-pixel pipeline through a generic virtual compressed stream
boundary. Composition begins with a fully transparent black IHDR-sized canvas
and implements SOURCE/OVER plus NONE/BACKGROUND/PREVIOUS. A first-frame
PREVIOUS is treated as BACKGROUND as required by the specification.

The inspectable stages are:

```text
Frame Output | Pre-Blend | Post-Blend | Post-Dispose
```

Only the selected frame's required artifacts are materialized. PREVIOUS stores
the affected region, not an unconditional full canvas copy. Canvas and stage
artifacts share a 64 MiB LRU budget. The engine attempts a post-dispose
checkpoint every 32 frames; checkpoints are evictable and random replay remains
cancelable.

Animation metadata limits are 100,000 indexed frames, 1,000,000 animation
chunks and 64 MiB of retained metadata. Exceeding a limit yields a bounded
partial result.

### 7.4 Timeline and adaptive UI

Playback is paused at animation frame 0 by default. Controls provide first,
previous, play/pause, next, last, direct frame entry, current time, loop state
and 0.25x/0.5x/1x/2x speed. `Ctrl+G` focuses direct frame entry. Selecting a
frame or X/Y pauses playback. `delay_den=0` is interpreted as 100; raw zero
delay remains visible while the effective playback delay is clamped to 10 ms.

APNG-specific UI is capability-driven, not extension-driven:

| Capability | APNG UI |
|---|---|
| Static PNG or invalid animation with zero verified frames | Completely hidden |
| Detection in progress | Hidden to avoid UI flicker |
| Valid APNG | Visible |
| Partial APNG with at least one verified frame | Visible, playback disabled |

For a valid APNG, viewing a separate static fallback retains the APNG controls
and labels it `Static fallback · not an animation frame`.

The timeline is a collapsible virtualized strip below Preview. Base static
Preview tabs retain their order; APNG stage tabs append conditionally. A
conditional Animation Inspector shows fcTL, delay, Blend, Dispose and canvas
position. The APNG Hex source is `Frame Stream`; File, Inflated and Defiltered
remain available.

The Open dialog and drag/drop accept `.png`, `.PNG`, `.apng` and `.APNG` through
one shared suffix predicate. Actual recognition always uses signature and
parsed chunks, never the filename.

On a fatal animation error, subsequent frames are not decoded, playback stops
and Image reverts to the static fallback. Verified frames remain manually
inspectable as Partial evidence.

## 8. Test and evidence principles

- Every behavior change starts with a focused failing test.
- Static PNG regression tests prove no APNG-only object, tab, timeline, action
  or settings key is visible or created for a static document.
- APNG fixtures cover both default-image layouts, one frame, Stored/Fixed/
  Dynamic streams, all Blend/Dispose values, sequence errors, cross-fdAT
  boundaries, zero delay, finite/infinite loops, partial decode and large
  virtualized timelines.
- Composition goldens use independently calculated expected RGBA values. They
  do not call the implementation under test to produce expectations.
- Release evidence records OS, architecture, Qt, DPI/device-pixel ratio,
  display protocol, corpus revision, cold/warm state and machine identifier.

## 9. Work Package documents

- `docs/development/wp-5u15-main-window-decomposition.md`
- `docs/development/wp-5u12-compression-inspector-completion.md`
- `docs/development/wp-5u14n-native-theme-evidence.md`
- `docs/development/wp-607-cross-platform-quality-evidence.md`
- `docs/development/wp-602b-statistics-ui-export-reentry.md`
- `docs/development/wp-699-706-apng-first-release.md`

Implementation plans are written only after this design and the six Work
Packages pass user review.
