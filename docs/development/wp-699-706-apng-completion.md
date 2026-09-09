# WP-699–706 — APNG First Release Completion

Status: **PASS** (2026-09-09; the 2026-09-09 merge review of `c1d7af2` found
the two GUI performance measurement errors below, both corrected in
`a8df8e2` and re-recorded — see the corrected baselines section. The same
merge review explicitly accepted the Homebrew Qt toolchain exception and
recorded the deferred Compression-tab decision. Fast-forward merged into
`main` at the same tree `3f58b43`.)

The latest review found that `timelineModelScrollBaseline` manipulates the
vertical scrollbar and forces its range, whereas the production timeline is
left-to-right without wrapping. Re-run using the actual horizontal range and
verify that visible frame indices advance. Also, the playback fixture's
delay fraction is 1/1 second, not the commented 10 ms: correct the intended
load and refresh the baseline before attributing eight publications in eight
seconds to worker throughput. These evidence issues remain separate from the
explicitly accepted Qt exception and deferred Compression issue below.

The 2026-09-08 completion claim in this file was previously withdrawn: the
earlier checks had not verified actual APNG playback or stage presentation.
The GUI implementation was subsequently corrected (snapshot commit `8a96119`,
"GUI acceptance corrective pass": animation controller/worker result gating,
real timeline controls, four dedicated stage views, selection navigation,
replay checkpointing and canvas budget, tests) and the remaining acceptance
work was completed and re-verified from that state. The historical claims
below the original table are superseded by the evidence in this section.

## Takeover corrective pass

- Branch `wp-699-706-apng-implementation` in worktree
  `.worktrees/wp-699-706-apng-implementation`; corrective-pass snapshot
  committed as `8a96119` and tagged
  `takeover/wp-699-706-apng-baseline` before the final acceptance work.
- The corrective pass was reviewed against the plan's hard constraints:
  generation/request-serial/identity triple gate in
  `AnimationController::onWorkerResult`; single in-flight frame worker with
  pending-frame relaunch; bounded thumbnail queue (32) and cache (256);
  selection/X-Y changes pause playback; canvas stage click coordinates apply
  the frame offset only for Frame Output; no APNG widgets in static
  documents; no decoding on the UI thread.

## Product gate (T17)

`tests/gui/apng_product_gate_test.cpp` (target `pnga_gui_apng_product_gate_tests`,
CTest name `gui_apng_product_gate_tests`) covers the frozen end-to-end flow:
open → default frame 0 paused → next/first navigation and real-time play/pause
→ four stage views (Frame Output, Pre-Blend, Post-Blend, Post-Dispose with
distinct expected pixels incl. BACKGROUND dispose) → Hex sources File / Frame
Stream / Inflated / Defiltered with IDAT label on static selection → partial
APNG mounts controls, shows verified frame 0 and keeps playback disabled →
static fallback selection returns StaticImage identity and IDAT label → close
destroys all APNG widgets → static PNG never creates animation UI. Fixtures
are generated in-code from `tests/common/apng_fixture.h`
(`make_apng_canvas`, byte-identical to `make_apng` at 1×1) — no external
assets added.

## Verification matrix (final tree, macOS arm64, Qt 6.11.1)

| Check | Result |
| --- | --- |
| `python3 scripts/verify_repository_layout.py` | exit 0 — 0 failures, 0 warnings |
| `python3 scripts/verify_dependencies.py` | exit 0 — 0 failures, 0 warnings |
| `cmake --preset dev` + `cmake --build --preset dev --parallel 4` | PASS (all targets) |
| `QT_QPA_PLATFORM=offscreen ctest --preset dev --no-tests=error --output-on-failure` | 63/63 passed |
| `cmake --preset asan` + `cmake --build --preset asan --parallel 4` | PASS (545 targets) |
| `QT_QPA_PLATFORM=offscreen ctest --preset asan -R 'apng\|png_format\|reconstruction\|analysis_engine\|fuzz'` | 6/6 passed |
| `QT_QPA_PLATFORM=offscreen ctest --preset asan --no-tests=error` | 63/63 passed |
| `python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --skip-build --jobs 4` | PASS (2 deterministic replays + fuzz smoke) |
| `python3 scripts/run_gui_gate.py` | PASS — 3 DPI gates (100/150/200%) |
| `python3 scripts/run_performance_corpus.py --preset dev --skip-build --enforce-thresholds` | PASS — thresholds `tests/performance/thresholds-v1.json` |
| `python3 scripts/run_package_smoke.py --preset release --jobs 2` | PASS — `png-analyzer-0.1.0-macOS-arm64.tar.gz`, CLI `pnga 0.1.0` |
| `git diff --check` | exit 0 |

The APNG performance record covers the three frozen WP-706 scenarios
(`pnga_performance_runner`, fixed-seed and budget-checked):

| Scenario | Recorded | Enforced |
| --- | --- | --- |
| `apng-metadata` | 100,000-frame metadata scan (68–70 ms, 8,000,144 B retained) | 64 MiB metadata budget |
| `apng-playback-100` | 100 small-frame sequential playback, cold P50/P95 ≈ 165/309 ms, warm P50/P95 ≈ 56/104 ms per frame, retained 2.5 MB | 64 MiB replay budget |
| `apng-random-jump-1000` | 1,000-frame animation, fixed seed 20260908, 8 cold + 292 warm jumps, cold P50/P95 ≈ 1.5/3.0 s, warm P50/P95 ≈ 2/23 µs, retained 25.3 MB | 64 MiB replay budget |

No approved APNG time thresholds exist, so the latency figures above are
recorded as baselines only; the machine-enforced limits are the frozen
64 MiB budgets (`retained_bytes` entries in `tests/performance/thresholds-v1.json`
plus in-runner assertions). Process RSS peak is recorded per scenario — the
readouts are true peaks (`getrusage ru_maxrss` with macOS byte units handled;
Windows `PeakWorkingSetSize`); the pre-existing `statistics-bounded-blocks`
scenario keeps the older current-size readout under the same field name and
is recorded here as an inherited issue rather than changed inside this work
package.

GUI-side baselines are recorded by
`tests/gui/apng_gui_performance_test.cpp` (CTest
`gui_apng_gui_performance_tests`; `PNGA_APNG_PERF_OUT` emits
`build/evidence/wp-699-706/apng-gui-performance.json` with the environment
and git commit). An earlier revision of this test drove the inert vertical
scrollbar (the timeline is a left-to-right, no-wrap view) and paced the
playback fixture at one second per frame while commenting 10 ms; both
measurement errors were corrected per review — the sweep now drives
`QListView::scrollTo` across the real horizontal axis and verifies that the
leftmost visible frame index advances (1,000 distinct positions over the
100,000-entry model), and the playback fixture paces at the corrected
10 ms/frame.

Corrected baselines (recorded at `a8df8e2`, native cocoa window): timeline
model population of 100,000 entries 8.9 ms; 1,000-step full-range
horizontal scroll sweep 1.24 s total, step P50 1.1 ms / P95 1.65 ms; the
leftmost visible frame index advances across 1,000 distinct positions and
reaches the end of the model. During real playback of the 100-frame
document at 10 ms/frame pacing, a 1 ms event-loop probe measured tick-gap
P50 4.0 ms / P95 16.3 ms / max 77 ms — no sustained main-thread freeze.
The publication rate inside the window was 127 publications (about 16 fps
against the 100 fps pacing target — the pipeline drops frames rather than
blocking the UI thread; the per-publication cost splits between the replay
materialize on the worker and the publication handlers on the main
thread).

## Native macOS evidence (cocoa, not offscreen)

Produced by running `pnga_gui_apng_product_gate_tests` on the native platform
(no platform override) with `PNGA_APNG_GATE_OUT=build/evidence/wp-699-706`:
all 7 cells executed against real windows, 7 screenshots under
`build/evidence/wp-699-706/captures/` (git-ignored) plus
`apng-product-gate-evidence.json` recording environment (macOS 26.6.2, arm64,
Qt 6.11.1, cocoa, device-pixel-ratio 2.0, logical DPI 72, window 1400x950,
commit `c19f473`) and SHA-256 of every capture and generated sample.

The capture was re-run at the final pre-merge tip `c19f473` after the
post-acceptance GUI refinements (empty-canvas hint, thumbnail outlines,
monospace Animation inspector text, "Static Fallback" tab label); the
earlier capture at `8a96119` is superseded.

| Cell | Result |
| --- | --- |
| valid-open-frame0-paused | captured (PASS) |
| navigation-playback-postblend | captured (PASS) |
| stage-frame-output | captured (PASS) |
| stage-post-dispose | captured (PASS) |
| static-fallback-selected | captured (PASS) |
| partial-playback-disabled | captured (PASS) |
| static-no-animation-ui | captured (PASS) |
| palette-frame-output | captured (PASS) |

The capture was refreshed again at `0a70b13` after the palette delivery
wiring landed; the palette cell drives a generated palette APNG (256-entry
PLTE) through the PLTE/tRNS -> FrameRequest::delivery -> deliver_rgba8 path
in the real window.

## Frame decode matrix and composition fuzz (T8/T16 closure)

`tests/unit/analysis-engine/frame_analysis_test.cpp` now carries the frozen
WP-702 frame-level matrix over `analyze_frame` with independently computed
delivered-RGBA expectations (the delivery rules — scale 255/max, 16-bit
`>>8`, palette lookup, tRNS comparison at the original depth — are
reimplemented in the test, not generated through the production path):

- full color-type/bit-depth matrix: gray 1/2/4/8/16, RGB 8/16, palette
  1/2/4/8 (with generated PLTE), gray+alpha 8/16, RGBA 8/16;
- Adam7 interlaced frames for RGBA8, gray16 and palette4 over an odd-sized
  canvas (partial first/last passes);
- tRNS at original depth for gray 8/1, RGB 16 and palette 4;
- multi-fdAT frames (payload partitioned across four fdAT chunks) deliver
  byte-identical pixels to a single-fdAT frame;
- subrect frames map the frame-local pattern with their fcTL offsets;
- corrupt frame streams (flipped zlib payload byte and an invalid filter
  byte, both with recomputed CRCs so indexing stays complete) fail with a
  stable error instead of partial pixels.

`tests/unit/png-reconstruction/canvas_composition_test.cpp` adds a seeded
(20260908) 4,000-iteration rect-arithmetic fuzz over blend×dispose: rects
deliberately hang off the canvas edges, accepted paths verify that bytes
outside the rect never change, PREVIOUS restores the pre-blend rectangle
exactly and BACKGROUND clears the rect to transparent black; rejected paths
must leave the canvas untouched (≈1.38 million assertions per run, covered
by the ASan suite).

The fixture gained `make_apng_format` (every color type/depth/interlace,
PLTE/tRNS emission, fdAT payload splitting) plus `refresh_chunk_crc` for
CRC-consistent corruption; `make_apng`/`make_apng_canvas` behavior is
unchanged. The engine gained `delivery_context_from(index, canvas_header)`
converting the retained PLTE/tRNS bytes into the frame delivery context,
and the GUI now fills `FrameRequest::delivery` with it — palette/tRNS APNG
files previously failed delivery in the GUI ("palette is missing"); this
closes that gap.

## Not executed (honest gaps)

- Windows and Linux native window interaction evidence: **not executed** on
  this platform; macOS results must not be extrapolated. The automated
  offscreen/ASan suites are platform-independent.
- Coverage-guided fuzz corpus growth beyond the deterministic replay gate is
  not configured in this environment; the sanitizer fuzz gate covers the
  deterministic replay + smoke level recorded above.

## Environment note: Qt provenance

On 2026-09-09 the user explicitly approved deferring the official Qt installer
migration for this merge and accepting the existing Homebrew Qt 6.11.1
verification environment. This is a scoped acceptance for WP-699–706, not a
general replacement of the repository dependency policy.

The local verification builds resolve Qt through
`/opt/homebrew/lib/cmake/Qt6` (Homebrew `qt` 6.11.1), not the official Qt
installer the repository dependency contract names. This is a pre-existing
characteristic of this machine's toolchain that every previously accepted
work package on this host was verified against (including the WP-5U14N
native theme evidence at Qt 6.11.1), not something this branch introduced.
It is recorded here for honesty; switching the host to the official
installer Qt is a repository-level infrastructure task outside this work
package's allowed paths.

## Deferred issue: Compression retains static fallback data

- Recorded and explicitly deferred by the user on 2026-09-09; accepted as a
  known limitation for this merge. No behavior change is included here.
- Reproduction: open the user-provided local sample
  `/Users/lijiangbo/project/png_overview/examples/apng/support/027.png`
  (static fallback plus two animation frames), then select an animation frame
  while viewing Compression. The panel continues to show fallback IDAT data,
  which can be mistaken for the selected frame's compressed data. The sample
  is referenced only; it has not been copied into the repository corpus.
- Cause: TraceOrchestrator builds a VirtualIDATStream for the static image;
  animation selection updates preview/Frame Stream Hex without replacing the
  Compression analysis context.
- Agreed future interaction: hide Compression when an animation frame is
  selected; if it was active, select Animation. Restore Compression for
  Static Fallback without stealing focus. Keep it hidden during playback and
  preserve ordinary static PNG behavior.
- Follow-up must also suppress stale Compression results and navigation while
  animation frames are selected. Test fallback -> frame -> fallback, active
  tab transitions, playback, late results and static PNG non-regression.
- Per-frame Compression support is a separate future capability; the present
  implementation must not be described as providing it.

## Changed paths since the withdrawn claim

- `apps/png-analyzer-gui/src/animation_controller.*`,
  `animation_worker.*`, `main_window.cpp`, `main_window_ui.*`,
  `selection_navigation_controller.*`
- `ui/qt/include/pnga/ui/qt/animation_inspector.h`,
  `animation_timeline.h`, `animation_timeline_model.h`, `ui/qt/src/` same
- `libs/analysis-engine/src/animation_replay.cpp` (canvas budget check,
  32-frame checkpoints)
- `tests/common/apng_fixture.h` (`make_apng_canvas`, `zlib_deflate`),
  `tests/gui/animation_controller_test.cpp`,
  `main_window_layout_test.cpp`, new
  `tests/gui/apng_product_gate_test.cpp`, `tests/gui/CMakeLists.txt`
- Post-acceptance UI refinements (`ea29333`, `fd1d721`, `40ae443`,
  `0e3090e`): empty-canvas hint in `DeliveredImageView` with
  dispose-semantics reasons, 1-px black thumbnail outlines, Animation
  inspector layout margins and monospace theme font, "Static Fallback"
  relabeling of the first preview tab for independent-fallback APNGs
- `docs/development/wp-699-706-apng-completion.md`,
  `docs/development/wp-699-706-apng-first-release.md`

The implementation plan lives at
`docs/superpowers/plans/2026-09-08-wp-699-706-apng-implementation.md`
(committed as `d6acbba` before feature work started).

---

## Historical record (superseded, kept for traceability)

The implementation was completed on branch
`wp-699-706-apng-implementation` in the independent worktree
`.worktrees/wp-699-706-apng-implementation`. The delivered scope covers the
WP-699 through WP-706 contracts:

- explicit static-image and animation-frame identity, stage keys and legacy
  selection compatibility;
- bounded APNG metadata indexing, sequence/geometry validation, verified-prefix
  handling and cancellation;
- borrowed virtual frame streams over IDAT/fdAT spans without full payload
  concatenation;
- shared frame decoding, RGBA delivery, frame output and canvas composition;
- SOURCE/OVER blend and NONE/BACKGROUND/PREVIOUS disposal with bounded replay
  and checkpoint storage;
- deterministic timeline/playback state, frame selection, four playback speeds,
  background frame workers and stale-result rejection;
- adaptive APNG UI, frame-stage tabs, virtual Frame Stream navigation, timeline
  and animation inspector; static PNG UI remains free of APNG-only controls;
- APNG parser mutation smoke coverage, public libpng static differential
  coverage, bounded coverage-driver input, and the 100,000-frame metadata
  performance scenario.

The earlier verification table from the withdrawn claim is retained above in
history only; it did not include native playback verification and was replaced
by the matrix at the top of this file.
