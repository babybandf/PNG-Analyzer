# WP-5U8 — Chunk Detail Panel

> Work Package: `WP-5U8`  
> Milestone: M5 UI Refinement  
> Status: approved for implementation  
> Depends on: WP-101, WP-104, WP-5U2, WP-5U7

## 1. Goal

Add a detail pane below the existing Chunk List. The pane follows the selected
physical chunk, displays its envelope information, and presents bounded,
structured fields for supported non-IDAT chunks. IDAT remains intentionally
opaque: only its basic envelope information is shown. The detail pane is
vertically resizable with the list and horizontally scrollable for long field
values.

## 2. Boundaries

Allowed paths:

- `docs/development/wp-5u8-chunk-detail-panel.md`
- `libs/png-format/include/pnga/png-format/chunk_detail.h`
- `libs/png-format/src/chunk_detail.cpp`
- `libs/png-format/CMakeLists.txt`
- `tests/unit/png-format/chunk_detail_test.cpp`
- `ui/qt/include/pnga/ui/qt/chunk_detail_panel.h`
- `ui/qt/src/chunk_detail_panel.cpp`
- `ui/qt/CMakeLists.txt`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `tests/gui/main_window_layout_test.cpp`

The GUI must not parse PNG bytes. `libs/png-format` owns bounded field decoding
and returns an immutable presentation-neutral view. The Qt layer only formats
the returned rows and manages selection/layout.

## 3. Data contract

`describe_chunk(source, node)` returns:

- chunk type and envelope offsets/length;
- a `basic_only` flag;
- deterministic name/value rows for supported fields;
- an explicit unsupported/truncated row when a known chunk has an invalid
  length or an unknown chunk schema is encountered.

All source ranges are checked before viewing. No complete IDAT payload is
copied, inflated or decoded. Variable text is bounded; malformed or oversized
payloads degrade to basic information plus an explanatory row.

The first implementation covers fixed/common metadata chunks: IHDR, PLTE,
tRNS, cHRM, gAMA, sRGB, pHYs, tIME, tEXt, zTXt and iTXt. IEND and IDAT show
basic information only. Other chunk types remain safely classified as
unsupported rather than being guessed.

## 4. UI contract

- The Chunk dock contains a vertical splitter: Chunk List above, Detail below.
- The splitter handle is user-adjustable and both panes remain usable at their
  minimum heights.
- The detail table uses two columns (`Element`, `Value`) and a horizontal
  scrollbar when values exceed the dock width.
- Selecting a chunk updates the detail pane; stale asynchronous results from a
  previous document or selection are discarded.
- Empty selection and no-document states are explicit and non-error.

## 5. Verification

- Unit tests cover IHDR, PLTE, IDAT basic-only behavior, text fields, malformed
  fixed-length chunks, unknown chunks and bounded oversized payloads.
- GUI tests cover the vertical splitter, horizontal scrollbar policy and
  selection-driven detail refresh.
- Run dev build, full CTest, GUI gate, repository layout audit and CI.

## 6. Non-goals

- No IDAT Inflate or DEFLATE analysis.
- No APNG frame semantics.
- No new parser dependency or third-party source.
- No raw whole-file or whole-IDAT copy for display.
