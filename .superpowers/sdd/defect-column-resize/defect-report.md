# Defect report — Compression sub-pages: column resize lost + truncated content

Worktree: `.worktrees/fix-compression-column-resize` (branch `fix-compression-column-resize`, base `ef33fff`)
Authorized: product owner 2026-09-05. Implementer session 2026-09-05.

## Status: DONE (22/22 baselines byte-identical, 57/57 suites)

## Commits

| SHA | Subject |
|---|---|
| `1a9ead6` | `test: require resizable columns on compression tables` (RED) |
| `03b70ec` | `fix: allow column resize on compression tables` (GREEN + truncation disposition) |

## RED evidence (commit `1a9ead6`, current code fails)

New assertions inside the existing render test functions (test-entry count stays 57):

- `tests/gui/block_inspector_test.cpp` — `rendersModelBackedTableWithNormativeColumns()`:
  `Current == Fixed`; `Number/Type/Final/Events/Scanlines == Interactive`;
  `InputBits/OutputBytes == Stretch` (see "Deviation" below).
- `tests/gui/huffman_inspector_test.cpp` — `modelRendersProjectionWithExactHeaders()`:
  `Symbol/Bits/Canonical/ReadOrder/UsesInResult == Interactive`; `Meaning == Stretch`.
- `tests/gui/decode_trace_inspector_test.cpp` — `modelRendersTypedRowsWithExactHeaders()`:
  `Current/Step/InputBits/Output == Interactive`; `Event == Stretch` (frozen).

Offscreen runs on pre-fix code:

```
BlockInspectorTest::rendersModelBackedTableWithNormativeColumns  FAIL
  Actual (header->sectionResizeMode(column)): ResizeToContents
  Expected: Interactive    (block_inspector_test.cpp:152)
  Totals: 8 passed, 1 failed
HuffmanInspectorTest::modelRendersProjectionWithExactHeaders     FAIL  (12 passed, 1 failed)
DecodeTraceInspectorTest::modelRendersTypedRowsWithExactHeaders  FAIL  (13 passed, 1 failed)
```

## GREEN implementation (commit `03b70ec`, files: the three allowed pages only)

- All ResizeToContents data columns switched to `QHeaderView::Interactive`;
  the fixed marker and Stretch fill columns keep their modes.
- Content-derived initial widths are re-applied at every publish via
  `QTableView::resizeColumnsToContents()` (same Qt computation as ResizeToContents
  mode — verified identical on 0-row, 3-row and 400-row models), plus
  `setColumnWidth(Current, 28)` re-applied after each Blocks refit:
  - `block_inspector.cpp`: constructor + `setFastIndex` (publish path)
  - `huffman_inspector.cpp`: constructor + `syncActiveTable` (covers `setView` and
    the table-kind switch, matching the pre-fix RTC recompute-on-model-swap)
  - `decode_trace_inspector.cpp`: constructor + `setView`
- User adjustments between publishes are never reset (Interactive sections are
  not touched except by the refits above).

## Width-preservation verification (pre-fix == post-fix, probe-linked real pages)

A throwaway offscreen probe linked against `libpnga_ui_qt.a` instantiated the real
pages with typed fixtures and measured every column at 320/360/480/600 px.
Pre-fix and post-fix widths are identical at every width, e.g. Blocks @360:
`28/16/57/36/107/106(+hidden)`; Huffman @360: `53/32/31/68/75/91`;
Decode @360: `54/35/66/123/72`. `QHeaderView` ResizeToContents vs
`resizeColumnsToContents()` produce byte-equal widths, and Stretch sections
re-assert their viewport-derived width after each container resize, so the
locked WP-5U12F captures are reproduced exactly.

## Baseline compare gate (mandatory)

```
python3 scripts/run_wp_5u12_gui_gate.py --preset dev --jobs 4 \
  --output build/gui-gate/wp-5u12/evidence.json \
  --capture-dir build/gui-gate/wp-5u12/captures --compare-baselines
→ WP-5U12 GUI gate: PASS (22 captures …)  evidence.json status=PASS
  case results: {'pass': 22} / 22; gates accessibility/clipboard/
  component_order/degradation_320/geometry_bands/keyboard/no_replay = pass
```
(Re-run after committing so `evidence.json` records `commit = 03b70ec`.)

## Truncation symptom — concrete mechanism and disposition

Programmatic reproduction on the real pages (probe): per visible column,
`header->sectionResizeMode`, `columnWidth(col)`, `sectionSizeHint(col)` and the
max delegate sizeHint over all rows, at 320/360/480/600 px. Pre-fix findings:

1. **Huffman `Meaning` (Stretch) @360 px: width 32 < content 84** → clipped
   cells/header. This exact squeeze is baked into the locked
   `huffman-360-light/dark.png` baselines.
2. **Decode `Event` (Stretch) @320 px: width 83 < content 125** → clipped
   ("Match len 18 / dist 7"). Below the 360 px capture floor, so no baseline
   shows it.
3. **Blocks `Current` marker (Fixed 28 px, header hint 54 px)** → the header
   label is always clipped; normative flow-ui §20.3 geometry present in all
   Blocks baselines.

Mechanism: Qt `Stretch` mode redistributes the remaining viewport width to the
fill columns and compresses them below their content width when the viewport is
narrower than the sum of content widths (Blocks InputBits/OutputBytes never
drop below content at ≥320 px because their content is small). The resize
defect itself is NOT the cause — ResizeToContents columns always measured
`width == content`.

Disposition: **frozen-normative, left unchanged.** The diagnosis pre-declared
candidate (a) frozen-normative; the widths of these Stretch/Fixed sections are
pixel-locked into the 22 baselines (Huffman Meaning @360) and the frozen
Event=Stretch assertions (product gate :728, responsive :396); converting them
to Interactive was probe-proven to diverge every Blocks/Huffman capture (e.g.
Huffman Meaning becomes 57 px vs locked 48 px @360). No baseline breakage was
allowed, so no code change. All content (Interactive) columns now measure
exactly content width at publish and scroll horizontally instead of truncating.

## Deviation from the literal GREEN prose (flagged)

The binding diagnosis described Blocks data columns as "ResizeToContents", but
`block_inspector.cpp` also had `InputBits`/`OutputBytes = Stretch` (and Huffman
`Meaning = Stretch`). The GREEN prose names only "Event stays Stretch". Keeping
those three fill columns in Stretch mode is forced by the hard 22/22
byte-identical baseline constraint; they keep the same fill semantics as the
explicitly kept Event column, and the tests document them as `Stretch`
expectations. The reported defect (user column resizing) is fixed for every
content column; Stretch sections are not user-draggable by Qt design.

## Gate results (in required order)

| Gate | Result |
|---|---|
| `cmake --build --preset dev --parallel 4` | exit 0 |
| `QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure` | **100% — 57/57** (assertions live inside existing test functions; per-binary Qt totals: blocks 9, huffman 13, decode 14) |
| `python3 scripts/run_gui_gate.py --preset dev --jobs 4` | exit 0, PASS |
| `python3 scripts/run_wp_5u12_gui_gate.py … --compare-baselines` | **22/22 PASS** (byte-identical) |
| `python3 scripts/verify_repository_layout.py && python3 scripts/verify_dependencies.py` | 0 failures / 0 warnings |
| `git diff --check` | clean; tree clean except untracked `.superpowers/` |

Changed paths: `ui/qt/src/{block_inspector,huffman_inspector,decode_trace_inspector}.cpp`,
`tests/gui/{block_inspector,huffman_inspector,decode_trace_inspector}_test.cpp` — exactly the six allowed files; no model/CMake/header changes.
