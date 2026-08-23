# WP-5U9 — Central Stage Pixel Process Views

> Work Package: `WP-5U9`
> Milestone: M5 UI Refinement
> Status: implemented (2026-08-23)
> Depends on: WP-306, WP-5U3B, WP-5U3C, WP-5U7
> Product reference: user-provided screenshot dated 2026-08-23

## 1. Requirement interpretation

Replace the sparse content of the central `Pixels`, `Filtered`, and
`Defiltered` tabs with coordinate-driven stage explanations. All three tabs
use the same visual language as the Reconstruction panel's **Pixel
neighborhood**:

- a five-column by three-row neighborhood centered on the selected coordinate;
- one independent matrix for each source channel;
- a clearly marked current cell;
- aligned axes, fixed-width values, boundary placeholders, and shared theme
  semantics;
- a compact calculation section explaining how the current value at that
  stage is obtained.

The three tabs are not three copies of Reconstruction. Each tab must remain
semantically limited to the stage named by the tab:

| Tab | Question answered | Canonical data |
|---|---|---|
| `Pixels` | What native source sample does this file encode at the selected pixel, and how is it extracted/assembled? | `NativeImage.samples` plus the corresponding defiltered byte/bit mapping |
| `Filtered` | Which filtered byte(s) produced by Inflate belong to the selected source sample position? | filter byte, `StageSet.filtered`, scanline span, byte/bit offsets |
| `Defiltered` | How does reverse filtering reconstruct the selected source byte(s)? | filtered `X`, actual `a/b/c`, predictor, modulo result, `StageSet.unfiltered` |

`Pixels` means PNG-native samples. It does not mean delivered RGBA and must not
silently apply palette resolution, transparency, bit-depth scaling, or alpha
insertion. `Defiltered` means reconstructed scanline bytes. It does not mean
the final displayed image.

## 2. Current baseline and observed gap

The implementation baseline at task creation is commit `6961d6e` on `main`.

- `ui/qt/src/pixel_viewport.cpp` renders a plain 3×3 string containing all
  channels in a single `[v0, v1, ...]` tuple. It is not channel-separated,
  does not keep the current pixel centered at image edges, and does not explain
  sample extraction.
- `ui/qt/src/stage_preview_view.cpp` renders only byte counts and format flags
  for `Filtered` and `Defiltered`; it does not display selected-pixel data.
- `ui/qt/src/stage_inspector.cpp` already contains the visual reference: a
  channel-separated 5×3 neighborhood, current/dependency highlights, and
  per-channel reconstruction text.
- `StageViewportProvider` currently serves only `Stage::kNative`; requests for
  `kFiltered` are explicitly not applicable.
- `MainWindow` already sends the locked coordinate and immutable `StageSet` to
  all three central views. The global DEC/HEX toggle currently refreshes the
  Reconstruction report only.

This work package extends the central views; it does not replace or remove the
right-side Reconstruction report.

## 3. Goals

1. Make `Pixels`, `Filtered`, and `Defiltered` useful for the currently locked
   image coordinate.
2. Give all three tabs a consistent, channel-separated neighborhood layout
   matching the Reconstruction visual language.
3. Show the stage-specific calculation beneath the neighborhood without
   mixing later-stage facts into an earlier-stage tab.
4. Correctly handle source channel count, bit depth, packed samples, image
   boundaries, and Adam7 pass coordinates.
5. Reuse immutable analysis results; coordinate or tab changes must not parse,
   inflate, or decode the file again.

## 4. Non-goals

- No changes to PNG parsing, Inflate, reverse-filter algorithms, or delivered
  image decoding.
- No full-image stage renderings and no additional full-size `QImage` objects.
- No palette/tRNS expansion inside `Pixels`, `Filtered`, or `Defiltered`.
- No duplicate DEFLATE token/Huffman trace in the `Filtered` tab; detailed
  compression provenance remains in the Compression Inspector.
- No change to the `Image` or `Filter Map` tabs.
- No new coordinate model, selection bus, numeric-base control, dependency, or
  visual-theme overhaul.
- No attempt to map a byte dependency to a logical neighbor when the mapping is
  not exact.

## 5. Common UI contract

### 5.1 Page structure

Each of the three pages contains one vertically scrollable, read-only view:

```text
<Stage title>                     DEC or HEX
coordinate / format / pass / row context

<Channel 1>
              x-2      x-1      x       x+1      x+2
y-1           value    value    value   value    value
y current     value    value    CURRENT value    value
y+1           value    value    value   value    value

<Channel 2> ...

Current value calculation
<stage-specific inputs, offsets, formula, and result per channel/byte lane>

Legend / format-specific note
```

The content must be selectable and copyable. Long calculations wrap rather
than increasing the central panel's minimum width. At narrow widths, the
neighborhood may scroll horizontally, but the page itself must remain usable
and must not resize the main-window docks.

### 5.2 Neighborhood geometry

- Default geometry is exactly 5 columns × 3 rows.
- For a non-interlaced logical-pixel view, columns are `x-2..x+2` and rows are
  `y-1`, `y current`, and `y+1`.
- The selected coordinate is always the middle cell of the middle row.
- Out-of-bounds cells remain in place and display `—`; the window must not
  shift inward at an image edge.
- Each channel uses identical column widths and axis positions.
- DEC/HEX changes value formatting without changing geometry or alignment.

### 5.3 Channels

Only source channels are shown:

| PNG color type | Channel sections |
|---|---|
| 0 | `Gray` |
| 2 | `R`, `G`, `B` |
| 3 | `Index` |
| 4 | `Gray`, `Alpha` |
| 6 | `R`, `G`, `B`, `A` |

Do not invent an Alpha reconstruction for color types 0, 2, or 3. Any
delivered `A=255` behavior remains outside these tabs.

### 5.4 Semantic markers and appearance

- Current cell: the same current-cell semantic token used by Reconstruction,
  with a distinct border and visible `current` label.
- `Filtered`: the current stage input is additionally labeled `X`.
- `Defiltered`: actual predictor inputs are labeled `a`, `b`, and/or `c` and
  use the dependency semantic token. The current cell and dependency cells
  must remain visually distinct.
- `Pixels`: no `a/b/c` marker is shown because those roles belong to reverse
  filtering, not the native-sample stage.
- Ordinary cells use the normal neighborhood style.
- Color is never the only signal: text markers and border differences are
  mandatory.
- Light and dark palettes must remain readable. Semantic colors/styles must be
  centralized or shared with Reconstruction rather than copied as a new set of
  unrelated literals.

## 6. Stage-specific content

### 6.1 `Pixels`

Header:

- `Native pixels (DEC)` or `Native pixels (HEX)`;
- image coordinate, PNG color type, bit depth, channel count;
- non-interlaced or Adam7 pass context.

Neighborhood:

- Each cell shows the PNG-native sample for that channel.
- Indexed images show the palette index only.
- The matrix is image-coordinate based, including for Adam7, because this tab
  represents the assembled native image rather than one pass scanline.

Current calculation, per source channel:

- 8-bit: `defiltered byte -> native sample`;
- 16-bit: show high byte, low byte, network byte order, and
  `sample = (high << 8) | low`;
- 1/2/4-bit: show defiltered packed-byte offset, bit range/mask, shift, and the
  extracted native sample;
- Adam7: show pass number, pass-local coordinate, and final image coordinate;
- indexed: end at `Index=n`; do not resolve PLTE/tRNS here.

### 6.2 `Filtered`

Header:

- `Filtered bytes (DEC)` or `Filtered bytes (HEX)`;
- image coordinate mapped to pass, pass-local row/column, stream row;
- filter type and filter-byte value/offset;
- clear statement that these are unsigned Inflate-output bytes before reverse
  filtering. Signed residual may appear only as a labeled secondary view.

Neighborhood:

- For byte-addressable 8-bit samples, each source-channel cell shows its raw
  filtered `X` byte.
- For 16-bit samples, each channel cell shows two byte lanes in file order,
  such as `H:0x12 L:0x34`; it must not present their concatenation as a
  filtered 16-bit sample.
- For packed 1/2/4-bit grayscale/indexed data, filtering is byte-oriented. A
  cell therefore shows the backing filtered byte and byte offset, with a
  `shared packed byte` note where multiple logical x positions map to it. Do
  not extract or label filtered bit slices as logical samples.
- The target cell is labeled `X/current`. No `a/b/c` dependency highlight is
  shown in this tab because those values come from already-defiltered bytes.

Current calculation, per channel or byte lane:

- show scanline span and exact data-byte offset after the filter byte;
- show the raw unsigned value and, when useful, `s8` interpretation;
- state `Inflate output -> filtered X`; do not inline token/Huffman decoding.

### 6.3 `Defiltered`

Header:

- `Defiltered bytes (DEC)` or `Defiltered bytes (HEX)`;
- image/pass/stream-row context;
- filter type and predictor formula.

Neighborhood:

- 8-bit source channels show reconstructed byte values.
- The current byte is labeled `current`.
- Only dependencies actually used by the row's Filter Type are highlighted:

| Filter | Dependency markers |
|---|---|
| None | none |
| Sub | `a` |
| Up | `b` |
| Average | `a`, `b` |
| Paeth | `a`, `b`, `c` |

- A boundary dependency that is defined as zero is explained in the
  calculation and is not attached to an out-of-bounds logical cell.
- 16-bit channels display high/low reconstructed byte lanes separately. Each
  lane has its own `X/a/b/c/predictor/recon` calculation.
- Packed rows display the reconstructed backing byte and its byte-oriented
  dependencies. The optional extracted sample may be shown as an annotation,
  but dependencies must not be relabeled as logical neighboring pixels.

Current calculation, per source channel/byte lane:

```text
None:    recon = X
Sub:     recon = (X + a) mod 256
Up:      recon = (X + b) mod 256
Average: recon = (X + floor((a + b) / 2)) mod 256
Paeth:   recon = (X + PaethPredictor(a, b, c)) mod 256
```

The displayed result must equal the corresponding byte in
`StageSet.unfiltered`. This tab stops at reconstructed bytes; native sample
assembly belongs to `Pixels`.

## 7. Adam7 and mapping rules

- `Pixels` uses the final image-coordinate neighborhood because native samples
  have already been assembled from passes.
- `Filtered` and `Defiltered` use the current Adam7 pass's scanline geometry.
  The center remains the selected image coordinate, while neighboring columns
  and rows are pass-local neighbors mapped back to image coordinates using
  `x_step` and `y_step`.
- Axis labels must state pass-local coordinates and may include mapped image
  coordinates. They must not imply that pass neighbors are adjacent final
  image pixels when the step is greater than one.
- Empty pass positions and pass boundaries display `—`.
- Predictor dependencies use previous/current pass scanlines, never a
  deinterlaced synthetic row.

## 8. Data and architecture contract

### 8.1 Presentation-neutral query

Add or minimally extend a Qt-free, bounded analysis-engine query that returns
the facts needed by all three views. The recommended shape is an additive
`StagePixelProcessView` model and `build_stage_pixel_process_view(...)` query.
The exact names may vary, but the result must include:

- status and stable error classification;
- requested stage, image coordinate, pass and pass-local coordinate;
- stream row, filter type, filter-byte offset/value;
- fixed grid geometry and in/out-of-bounds state;
- source channel names/indices;
- per-cell byte values and/or native sample values;
- byte offsets and bit spans;
- whether multiple logical coordinates share a packed byte;
- target byte lanes and actual `X/a/b/c/predictor/recon` events;
- boundary-zero reasons and exact logical-pixel mapping availability.

The Qt layer owns strings, fonts, colors, HTML/widgets, DEC/HEX formatting, and
layout only. It must not derive PNG row layout, read packed bits, reconstruct a
filter, or infer dependencies from final RGB values.

The query must be deterministic, checked for arithmetic overflow, bounded to
the requested neighborhood, and operate on the immutable `StageSet`. It must
not copy a complete stage or retain a second image-sized artifact.

### 8.2 Reusable Qt presentation

Use one stage-aware central view component instantiated for `Native`,
`Filtered`, and `Defiltered`. Extract/reuse the Reconstruction neighborhood's
semantic styling or rendering helper so the two locations cannot drift in
meaning. Do not create three independent formatters with duplicated channel,
boundary, and theme rules.

The existing `StagePreviewView` remains responsible for `Filter Map`. The old
`PixelViewport` and the `Filtered`/`Defiltered` summary instances may be
replaced or retired after all call sites and tests migrate; do not leave dead
production paths.

### 8.3 State and threading

- `StageSet` remains produced on the existing worker thread.
- Views consume `shared_ptr<const StageSet>` and a coordinate; they do not own
  decoding work.
- Selecting a pixel, editing locked X/Y, changing numeric base, switching the
  file, or receiving a new valid StageSet refreshes the views.
- Hidden-tab refresh may be lazy, but a tab must be current when first shown.
- A stale document generation must never publish into the current views.
- Tab/coordinate changes must not reset dock sizes, lock state, other
  Inspector tabs, or Hex position.
- The global DEC/HEX control updates all three stage views and Reconstruction.

## 9. Empty, partial, and error states

Use stable, user-facing states:

- no document: `Open a PNG to inspect stage data.`
- analysis in progress: `Loading stage data…`
- coordinate out of range: show the requested coordinate and valid bounds;
- stage unavailable/partial: retain all validated context and explain the
  unavailable section;
- packed/Adam7 mapping not representable as a logical pixel dependency: show
  source-byte facts and a precise limitation note;
- internal details remain in logs; do not expose raw strings such as
  `row query: error`.

An unsupported mapping is not permission to display a plausible-looking but
incorrect logical-pixel matrix.

## 10. Implementation tasks and order

### T0 — Freeze examples and discriminating tests (P0)

- Add independent expected-value fixtures for RGB8 filter types, packed
  grayscale/index, RGBA16, and Adam7.
- Freeze exact channel labels, marker roles, and DEC/HEX representations.
- Cheapest discriminating test: for an RGB8 Paeth row at a non-boundary
  coordinate, assert that the three views return different target-stage
  values; only `Defiltered` exposes `a/b/c`, and its result equals the stored
  unfiltered byte.

### T1 — Qt-free stage pixel process query (P0)

- Implement the bounded presentation-neutral model/query.
- Reuse `compute_scanline_layout`, `filter_formula`, `StageSet.scanlines`,
  `StageSet.filtered`, `StageSet.unfiltered`, and `NativeImage`.
- Cover arithmetic, bounds, pass mapping, byte lanes, packed-byte sharing, and
  stable status values with unit tests.

### T2 — Shared central stage view and neighborhood renderer (P0)

- Implement the common scrollable/copyable page.
- Share current/dependency/ordinary style semantics with Reconstruction.
- Render the three stage-specific headers, matrices, calculations, legends,
  and limitation notes.
- Preserve usable layout at the central area's minimum width and at 150%/200%
  scale.

### T3 — Main-window integration and state synchronization (P0)

- Replace the current `Pixels`, `Filtered`, and `Defiltered` content widgets.
- Keep tab names/order unchanged:
  `Image | Pixels | Filter Map | Filtered | Defiltered`.
- Connect immutable StageSet, locked coordinate, document reset, and global
  numeric-base changes.
- Verify repeated file switches and stale worker completion cannot restore old
  content.

### T4 — GUI tests, visual evidence, and regression (P0)

- Add semantic GUI assertions that do not depend only on screenshot pixels.
- Capture representative manual screenshots.
- Run the full GUI and repository gates in Section 13.

## 11. Allowed and forbidden paths

Primary allowed paths:

- `docs/development/wp-5u9-stage-pixel-process-views.md`
- `libs/analysis-engine/include/pnga/analysis-engine/`
- `libs/analysis-engine/src/`
- `libs/analysis-engine/CMakeLists.txt`
- `libs/analysis-engine/README.md`
- `tests/unit/analysis-engine/`
- `ui/qt/include/pnga/ui/qt/`
- `ui/qt/src/`
- `ui/qt/CMakeLists.txt`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `tests/gui/`

Only files directly required by this work package may change within those
directories. An additive public analysis-engine model must be documented in
the owning README/interface comments.

Forbidden paths and changes:

- `third_party/`, package manifests, and dependency locks;
- PNG parser, Inflate/DEFLATE decoder, reverse-filter implementation, libpng
  backend, and unrelated validation/statistics code;
- accepted ADRs or repository dependency direction;
- generated files, external corpus files without manifest provenance, and
  unrelated work-package documents.

If correct stage facts cannot be expressed without changing a decoder result
or an accepted architecture decision, stop with `BLOCKED` and request a
separate approved task.

## 12. Acceptance and test matrix

### 12.1 Common behavior

- All three pages show a 5×3 centered neighborhood for each valid source
  channel.
- Top/bottom/left/right edges and all four corners retain the center position
  and show `—` outside valid bounds.
- RGB, RGBA, Gray, Gray+Alpha, and Index expose only their real source channels.
- Current/dependency/ordinary states are distinguishable by marker and border,
  in light and dark palettes.
- DEC/HEX updates all values and offsets without changing alignment.
- Page text and calculations can be selected and copied.

### 12.2 Stage correctness

- `Pixels` values exactly match `NativeImage.samples` and explain 8-bit,
  packed, or 16-bit sample construction.
- `Filtered` values exactly match the associated bytes in
  `StageSet.filtered`; the filter byte is not mistaken for channel data.
- `Defiltered` values exactly match `StageSet.unfiltered`.
- None/Sub/Up/Average/Paeth display the correct formula and actual dependency
  roles.
- First row/first byte boundary dependencies use zero with an explicit reason.
- `Filtered` does not show `a/b/c` as if they belonged to filtered input.
- `Defiltered` does not claim to be native/delivered RGBA.
- `Pixels` indexed output ends at Index and does not invent palette RGBA.

### 12.3 Format coverage

At minimum, automated tests cover:

1. RGB8 with each of the five filter types.
2. RGBA8 channel count and Alpha source channel.
3. Grayscale 1/2/4/8-bit, including packed-byte sharing.
4. Indexed 4/8-bit with `Index` channel semantics.
5. Gray+Alpha8.
6. RGB16 or RGBA16 high/low byte lanes and big-endian sample assembly.
7. Adam7 pass-local neighborhood mapping and pass boundaries.
8. Coordinate overflow/out-of-range, no-stage, and malformed partial data.
9. Repeated coordinate changes and at least ten alternating file loads.

### 12.4 GUI regression

- `Image` and `Filter Map` behavior is unchanged.
- Pixel lock, X/Y editing, Escape, arrow nudge, Hex-follow setting, Inspector
  selection, and workspace sizes remain stable.
- Switching among the three stage tabs does not trigger a new file analysis.
- No duplicate signal handling, visible stale result, UI-thread decode, crash,
  or new runtime warning.
- The central panel does not acquire an image-dependent minimum width.

## 13. Verification commands

Run from the repository root:

```sh
cmake --build --preset dev -j2
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/verify_repository_layout.py
git diff --check
```

Also perform manual checks on macOS at normal scale and at least one high-DPI
scale with RGB8, packed, 16-bit, Adam7, and edge coordinates.

## 14. Delivery evidence

- Implemented Qt-free model/query and focused unit tests.
- Shared central stage view plus any extracted semantic renderer/style helper.
- Main-window wiring and GUI tests.
- Before/after screenshots for:
  - RGB8 `Pixels` with R/G/B matrices;
  - RGB8 `Filtered` with `X/current`;
  - Average or Paeth `Defiltered` with correct dependency markers and formulas;
  - an image-corner coordinate showing fixed placeholders;
  - one packed or 16-bit case;
  - one Adam7 pass case.
- Changed-path list, verification commands/results, and confirmation that no
  decode/reparse work was added to the UI thread.

## 15. Definition of done

This work package is complete only when:

1. The three central tabs contain stage-specific pixel-process data rather
   than tuple text or byte-count summaries.
2. Each tab uses the common channel-separated 5×3 neighborhood presentation.
3. `Pixels`, `Filtered`, and `Defiltered` values are sourced from their
   canonical stage artifacts and are not cross-labeled.
4. Packed, 16-bit, boundaries, and Adam7 are either represented exactly or
   degraded to explicit source-byte facts without invented pixel semantics.
5. All coordinate, numeric-base, file-generation, and layout behavior remains
   synchronized.
6. Focused and full regression gates pass with no architecture-boundary or
   repository-layout violation.

## 16. Implementation evidence (2026-08-23)

- Added the Qt-free `StagePixelProcessView` projection with checked bounds,
  packed-byte sharing, 16-bit lanes, pass-local mapping and filter formula
  facts in `libs/analysis-engine/`.
- Replaced the central `Pixels`, `Filtered` and `Defiltered` placeholders with
  one shared scrollable/copyable Qt renderer instantiated for each stage.
  DEC/HEX changes refresh all three views without re-analysis.
- Added focused Catch2 and Qt GUI tests:
  `pnga_analysis_engine_tests` (77 cases),
  `gui_stage_pixel_process_view_tests`, and the existing MainWindow/Gate suite.
- Verification completed with the dev build, focused tests, and the required
  macOS application bundle rebuilt at
  `build/dev/apps/png-analyzer-gui/pnga_analyzer_gui.app`.
