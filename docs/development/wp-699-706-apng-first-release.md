# WP-699–706 — APNG First Release

Status: **design approved; pending written-package review** (2026-09-01)

Normative reference: W3C PNG Specification, Third Edition, sections 4.9,
11.3.6 and 13 error handling. Compare remains deferred.

## Goal

Implement APNG Chunk/sequence validation, virtual per-frame compressed streams,
frame reconstruction, SOURCE/OVER composition, NONE/BACKGROUND/PREVIOUS
disposal, inspectable canvas stages and a virtualized playback timeline without
regressing or visually changing ordinary static PNG analysis.

## Dependencies

- WP-5U15: PASS.
- WP-600A/B/C validation architecture: PASS.
- WP-607C fixture provenance rules apply.
- WP-602 Statistics may proceed independently; APNG statistics are not schema
  v1 and must not be added opportunistically.

## Global invariants

- Static PNG remains the default path and creates no APNG-only widget/action.
- Static fallback image and animation frame 0 have distinct identities.
- IDAT/fdAT payload is virtual and windowed; no complete concatenation.
- Frame decode and composition run off the UI thread, are cancelable and carry
  document generation plus frame identity.
- All dimensions, offsets, sequence counts, frame counts, durations, canvas
  bytes and replay work use checked arithmetic and explicit budgets.
- A later animation error preserves verified prefix frames but stops playback
  and prevents decoding subsequent frames.
- libpng remains the static Reference Backend; no private/patched libpng APNG
  API or new third-party decoder is authorized.

## Allowed paths

- `libs/png-format/**`, `libs/png-reconstruction/**`, `libs/trace-model/**`
- `libs/validation/**`, `libs/analysis-engine/**`
- `ui/qt/**`, `apps/png-analyzer-gui/**`
- focused tests, corpus generators/manifest, performance/fuzz runners
- ADR-0004/0007 clarification and APNG user/developer documentation

## Forbidden paths

- `third_party/**`, libpng private structures, full frame-stream concatenation
- one full Frame Output + three canvas images retained for every frame
- APNG UI in static documents, extension-only format recognition
- Compare, animation export/editor, audio, variable playback speed beyond the
  approved four values, or persistence of a running playback position

## WP-699 — Image identity and Stage contract

Replace ambiguous frame-only identity with:

```text
ImageIdentity = StaticImage | AnimationFrame(uint32 index)
```

Embed it in ImageCoordinate and artifact keys. Extend Stage with Frame Output,
Pre-Blend, Post-Blend and Post-Dispose. Update deterministic Selection
serialization with an explicit version/identity token; parse legacy static
`frame=0` records as StaticImage and reserialize canonically. Sentinel values
are forbidden.

Clarify ADR-0004 and ADR-0007 before code merge. Tests cover legacy round-trip,
static fallback vs frame 0 inequality, merge idempotence, huge/malformed frame
numbers and stage parse/serialize. Public model changes are limited to this
package; downstream packages consume the frozen contract.

## WP-700 — APNG Chunk, sequence and validation model

Add explicit big-endian parsers for acTL (8 bytes), fcTL (26 bytes) and fdAT
(minimum 4-byte sequence number). Return immutable AnimationIndex containing
AnimationControl, ordered FrameRecords, data Chunk spans, static-image role,
status, verified prefix and stable diagnostics.

Validate:

- exactly one acTL before first IDAT, nonzero uint32 num_frames and equality to
  fcTL count for complete results;
- shared fcTL/fdAT sequence begins at 0, increases by 1, with no gap/duplicate;
- each frame has one fcTL before its IDAT/fdAT data;
- width/height > 0, checked x+width/y+height within IHDR canvas;
- default-image fcTL is before IDAT, full canvas and zero offset;
- delay denominator 0 normalizes to 100 while raw fields are preserved;
- dispose values 0..2 and blend values 0..1;
- resource limits of 100,000 retained FrameRecords, 1,000,000 animation chunks
  and 64 MiB retained metadata. A larger syntactically valid declaration is a
  budget-limited Partial result, not a format violation.

Unknown/invalid values produce stable rule IDs and exact Chunk offsets. On the
first fatal ordering/sequence error, preserve prior FrameRecords and stop
claiming later frames. Chunk CRC validation remains owned by validation.

## WP-701 — VirtualFrameStream

Implement a borrowed logical stream over one frame's compressed data. If the
static image is frame 0 it references IDAT payload. Other frames reference fdAT
payload excluding each four-byte sequence number. Reads may cross Chunk
boundaries; logical-to-physical mapping returns all data-only spans and never
includes length/type/sequence/CRC bytes.

The stream holds shared source ownership or a documented lifetime token. Test
empty/truncated fdAT, one/many chunks, cross-boundary zlib header/token/Adler,
large offsets, checked end arithmetic, random window reads and mapping
round-trip. No vector proportional to compressed frame size is allowed.

## WP-702 — Frame decode and provenance

Generalize stage analysis to consume a virtual compressed-stream interface
implemented by both VirtualIDATStream and VirtualFrameStream. Do not duplicate
Inflate/filter/native conversion. Frame header inherits IHDR bit depth, color
type, compression/filter/interlace and document palette/tRNS context, while
width/height use fcTL.

Return immutable FrameStageSet with ImageIdentity, FrameControl, scanlines,
filtered/unfiltered/native/delivered artifacts, status and provenance. Work is
scheduled by frame with cancellation, generation checks and existing checked
image budgets. Decode only requested/current/prefetch frames; opening an APNG
does not materialize all frames.

Tests cover every PNG color type/allowed depth, Adam7, palette/tRNS, 16-bit,
first IDAT frame, separate static fallback, fdAT multi-span, corrupt zlib/filter
and stale/cancelled results.

## WP-703 — Blend, Dispose and Canvas Trace

Use an IHDR-sized straight-alpha RGBA canvas initialized to transparent black
at each play. Produce on demand:

1. Frame Output: delivered frame rectangle before composition;
2. Pre-Blend: canvas immediately before applying the frame;
3. Post-Blend: canvas after SOURCE or integer-exact OVER;
4. Post-Dispose: canvas after NONE, BACKGROUND clear or PREVIOUS restore.

SOURCE replaces RGBA in the frame rect. OVER uses checked integer arithmetic
and independently golden-tested alpha math. PREVIOUS saves only the affected
pre-blend rectangle; on first frame it behaves as BACKGROUND. Pixels outside
the rect never change.

Artifacts and post-dispose checkpoints use one 64 MiB LRU budget. Attempt a
checkpoint every 32 frames; if it cannot fit, retain fewer checkpoints and
replay from the nearest earlier retained state. Replay reports frame progress,
is cancelable and never publishes stale canvas. A single artifact larger than
budget returns Partial/too-large instead of bypassing the cache.

## WP-704 — Timeline and playback model

Create a Qt-free AnimationTimeline with frame index, raw delay fraction,
effective duration, cumulative checked start time, loop count and verified
range. Create a deterministic playback state machine for Paused, Playing,
WaitingForFrame, Ended, Partial and Error.

Approved behavior:

- default paused on AnimationFrame(0);
- first/previous/next/last and direct numeric frame jump;
- Ctrl+G is reserved by GUI for direct frame input;
- 0.25x, 0.5x, 1x and 2x only;
- raw delay numerator 0 remains visible; effective delay clamps to 10 ms after
  speed adjustment; denominator 0 is treated as 100;
- num_plays 0 loops indefinitely; finite play stops on final post-blend frame;
- manual frame, pixel or X/Y selection pauses;
- an unavailable target enters WaitingForFrame and commits only same-generation
  results; error/partial never auto-skips an unverified frame.

Tests use a fake clock; wall-clock sleeps are forbidden.

## WP-705 — Adaptive APNG GUI

APNG UI is driven by parsed AnimationCapability, never suffix alone:

| Capability | APNG-specific UI |
|---|---|
| detecting | hidden to avoid flicker |
| static or invalid with zero verified frames | completely absent/hidden |
| valid | visible |
| partial with at least one verified frame | visible; playback disabled |

Static documents must not create/show a Timeline, Animation tab, APNG stage tab
or Frame Stream label. Existing object names, tab indexes and keyboard order
remain unchanged for static PNG.

For APNG, append Frame Output, Pre-Blend, Post-Blend and Post-Dispose after the
existing fixed Preview tabs. Add a collapsible virtualized timeline below
Preview and a conditional Animation Inspector. Top-level Inspector order is
Reconstruction, Compression, Statistics when built/present, then Animation.
Animation Inspector shows raw and
effective delay, frame rect, sequence, blend, dispose, loop and status.

Timeline creates only visible delegates and supports first/previous/play-next/
last, direct frame input, current time, loop display and speed. Selecting frame
or X/Y pauses. A valid APNG static fallback keeps the controls visible and is
labeled `Static fallback · not an animation frame`.

Hex sources for APNG are File, Frame Stream, Inflated and Defiltered. Frame
Stream navigates the current frame's virtual compressed data and all physical
spans. Static Hex labels remain File, IDAT, Inflated and Defiltered.

Use one shared case-insensitive suffix predicate for File Open and drag/drop.
The picker is exactly:

```text
PNG/APNG files (*.png *.PNG *.apng *.APNG)
```

`openFile()` still validates signature/content and may open a valid APNG with
either extension. Tests cover picker filter, `.apng` drag/drop, renamed files,
static adaptive absence, valid/partial transitions and rapid file switching.

## WP-706 — APNG conformance, differential and performance Gate

Generate provenance-recorded fixtures for both default-image layouts,
single/many frames, all Blend/Dispose combinations, subrect offsets,
transparent/opaque edges, raw delays 0 and denominator 0, finite/infinite loops,
multi-fdAT boundaries, all DEFLATE types, palette/tRNS, 16-bit, Adam7, sequence
gap/duplicate/order errors, bad frame count/geometry, truncated frame and Adler
mismatch.

Composition goldens are independently calculated RGBA arrays. Differential
tests may use only an approved public oracle already within dependency policy;
absence of an APNG-capable libpng public API does not authorize a patched
dependency. Fuzz APNG body parsers, sequence builder, virtual frame reads and
composition rect arithmetic under ASan/UBSan.

Performance records index 100,000 metadata-only frames, play 100 small frames,
randomly jump in a 1,000-frame animation, measure cold/warm replay P50/P95,
timeline scrolling, peak RSS and UI-thread blocking. Static PNG performance
must remain within existing WP-604 thresholds.

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev -R 'selection|png_format|validation|reconstruction|apng|main_window|gui' --output-on-failure
ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev
python3 scripts/run_package_smoke.py --preset release --jobs 2
git diff --check
```

## Completion definition

`PASS` requires WP-699 through WP-706, static UI non-regression, conformance
goldens, bounded random access and native GUI evidence. Ambiguous image
identity, suffix-based recognition, hidden truncation, unbounded per-frame
storage, stale publication, partial APNG UI in static files or any Compare
surface is `FAIL`. A required ADR/layout/dependency change outside the approved
WP-699 clarification is `BLOCKED`.
