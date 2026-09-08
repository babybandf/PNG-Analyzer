# WP-699–706 — APNG First Release Completion

Status: **PASS** (2026-09-08, verified on the final tree at commit recorded
below)

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

The APNG metadata performance scenario (100,000 frames, 200,002 animation
chunks) remains in the enforced run; the recorded scan retained
8,000,144 bytes of metadata. Time metrics have no approved APNG thresholds
and are reported as baselines only.

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

## Not executed (honest gaps)

- Windows and Linux native window interaction evidence: **not executed** on
  this platform; macOS results must not be extrapolated. The automated
  offscreen/ASan suites are platform-independent.
- Coverage-guided fuzz corpus growth beyond the deterministic replay gate is
  not configured in this environment; the sanitizer fuzz gate covers the
  deterministic replay + smoke level recorded above.

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
