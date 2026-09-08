# WP-699–706 — APNG First Release Completion

Status: **FAIL — previous completion claim withdrawn** (2026-09-08)

The earlier checks did not verify actual APNG playback or stage presentation.
The completion claims below are historical and are not acceptance evidence.
GUI implementation and end-to-end verification are being corrected.

The implementation was completed on branch
`wp-699-706-apng-implementation` in the independent worktree
`.worktrees/wp-699-706-apng-implementation`. The implementation plan is present
at `docs/superpowers/plans/2026-09-08-wp-699-706-apng-implementation.md` and
was committed independently as `d6acbba` before feature work started.

The delivered scope covers the WP-699 through WP-706 contracts:

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

Verification completed on macOS arm64 with Qt 6.11.1:

| Check | Result |
| --- | --- |
| `python3 scripts/verify_repository_layout.py` | 0 failures, 0 warnings |
| `python3 scripts/verify_dependencies.py` | 0 failures, 0 warnings |
| `cmake --build --preset dev --parallel 4` | PASS |
| `QT_QPA_PLATFORM=offscreen ctest --preset dev --no-tests=error --output-on-failure` | 62/62 passed |
| `python3 scripts/run_gui_gate.py` | PASS; 3 DPI gates passed |
| `python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --skip-build --jobs 4` | PASS |
| ASan differential/parser/PNG-format/analysis-engine subset | 4/4 passed |
| ASan `gui_apng_controller_tests` | 1/1 passed |
| `python3 scripts/run_performance_corpus.py --preset dev --skip-build --enforce-thresholds` | PASS |
| `python3 scripts/run_package_smoke.py --preset release --jobs 2` | PASS; CLI package smoke passed |
| `git diff --check` | PASS |

The APNG performance record indexed 100,000 frames with 200,002 animation
chunks, retained 8,000,144 bytes of metadata, and completed the metadata scan
in 68,537 microseconds in the recorded run. Native GUI capture and cross-
platform layout gates also passed in the dev CTest suite.
