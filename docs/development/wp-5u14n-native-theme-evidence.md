# WP-5U14N — Native Windows/macOS Theme Evidence

Status: **approved; frozen for implementation** (2026-09-04)

Written-package review: `wp-5u14n-written-package-review.md` (rulings R1–R7 bind
the implementation plan). Frozen amendments: R1 authorizes the test-only
capture target `pnga_gui_wp_5u14n_native_capture_tests`; R6 corrects this
section's verification command (`pnga_gui_tests` does not exist — use
`pnga_gui_application_theme_tests`).

## Goal

Close the only remaining WP-5U14 acceptance gap with reviewable native
Windows and macOS screenshots and interaction evidence. This package does not
redesign the implemented theme.

## Dependencies

- WP-5U15 and WP-5U12F: PASS so evidence covers the final static UI.
- WP-5U14 automated theme tests: PASS.

## Allowed paths

- evidence output under `build/evidence/wp-5u14n/` (not committed)
- a deterministic capture runner under `scripts/` if automation is needed
- focused GUI test changes only for a reproducible native defect
- `docs/development/` evidence record and this document

Production theme changes require a separately reported defect and focused test;
they are not silently absorbed into evidence collection.

## Required matrix

Capture native application windows, not offscreen rendering:

| Platform | Modes | Scale | Required views |
|---|---|---|---|
| Windows stable x64 | System Light, Light, Dark, System Dark | 100%, 150%, 200% | default, Compression Blocks/Huffman/Decode, narrow Inspector, focus state |
| macOS stable arm64 | System Light, Light, Dark, System Dark | native Retina plus one logical scaled case | same views |

Each capture record includes OS build, architecture, Qt version, theme request
and effective mode, logical DPI, device-pixel ratio, window size, git commit,
fixture SHA-256 and capture timestamp in UTC.

## Manual checks

- selected tabs use at least two cues and remain visible in inactive windows;
- hover, focus, pressed, disabled, error, Current and Selection are distinct;
- no label/button/bit string is clipped at approved widths;
- fixed-pitch data remains readable and focus rings are not overwritten;
- System mode reacts to an OS color-scheme change without reopening the file,
  resetting selection or moving docks;
- Restart persists explicit Light/Dark; Reset Layout does not reset theme.

## macOS evidence (automated cells)

Formal clean-directory run, 2026-09-04 (branch tip `a125a5d`), produced by
`python3 scripts/run_wp_5u14n_capture.py --platform macos --preset dev --jobs 4`;
runner exit 0, `evidence.json` status `PASS`. Host: macOS Tahoe 26.6.2, arm64,
Qt 6.11.1, native Retina (logical DPI 72, device-pixel ratio 2.00), window
1200x760. Evidence tree: `build/dev/evidence/wp-5u14n/` (git-ignored).

Evidence record `evidence.json` SHA-256:
`c1febc87c91d891232f372bf734c81603349d233ac7721507e2bdc9df3e618b6`

| Cell id | Views captured | Record reference (record SHA-256) | Result |
|---|---|---|---|
| mac-light-retina | 7/7 (default, blocks, huffman, decode-trace, narrow-inspector, focus, stored) | `3871305b257918e108ed1a59083c243f1f287474f98bd070f9850123db9521c1` | captured (PASS) |
| mac-system-light-retina | 7/7 (same views; OS Light via appearance flip, fresh process) | `5b2de5371c0755effdfd8fa9abacef7849ac43e02d3237e09fb469eeee0688a0` | captured (PASS) |
| mac-dark-retina | 7/7 (same views) | `f2e5e886d2c62f3d9aa842f26e576ebf32002bd229193f224a7749b8b2218491` | captured (PASS) |
| mac-system-dark-retina | 7/7 (same views; OS Dark via appearance flip, fresh process) | `269fc1679f98301bdd2cd124580b568f543facc0a2d2fbae4fc76e66d1ab1a42` | captured (PASS) |

Captures: 4 cells x 7 views = 28 PNGs under `captures/`, one validated
`pnga-wp5u14n-native-capture-v1` record per cell under `records/`; every
record passed the runner's R2 field/schema/fixture-SHA/PNG-SHA validation.

Appearance restore: pre-run state `light` was saved before the first System
flip and re-applied after the run (`appearance restored: light (verified)`,
readback equals saved state; `evidence.json` `appearance.restored=true`).

Automated/manual split (R3): the four theme cells above are the macOS
automated units at native Retina. `mac-light-scaled` (one logical scaled case)
and the macOS half of the §Manual checks interactive items M1–M6 remain manual
units for the product owner (Task 5); they are not covered by this run.

## Windows CI evidence (automated cells)

Workflow: `.github/workflows/native-theme-capture.yml` (`workflow_dispatch`
only — never triggered by push/PR, so CI minutes stay bounded), job
`windows-capture` on `windows-latest`. It mirrors ci.yml precedents
(setup-python 3.11 + `PyYAML==6.0.2`, official MSVC DevShell with persisted
environment, pinned vcpkg tool checkout + binary caches,
`python scripts/bootstrap.py`, `cmake --preset dev`), installs pinned
official Qt 6.8.3 binaries (`win64_msvc2022_64`) through the pinned
aqtinstall 3.3.0 downloader (CMAKE_PREFIX_PATH plus the runtime `bin` dir on
PATH), builds `pnga_generate_wp607c_corpus` plus the R1 capture target
`pnga_gui_wp_5u14n_native_capture_tests`, materializes the corpus fixture,
then runs the runner with `--platform windows` (System Light/Dark via
registry `AppsUseLightTheme` flip + `WM_SETTINGCHANGE` broadcast +
verification read-back, fresh process per cell) and uploads
`build/dev/evidence/wp-5u14n/` as artifact `wp5u14n-native-capture-windows`
(`if: always()`). If the hosted session cannot render, the target/runner
record honest failures; cells are never faked.

Recorded run: 2026-09-04, workflow commit `730a8cca`,
<https://github.com/babybandf/PNG-Analyzer/actions/runs/33858698456> —
`evidence.json` status `PASS`, 4 cells x 7 views = 28 PNGs (artifact
verified). Host: Windows Server 2025 Version 24H2 (the hosted desktop session
renders natively), x64, Qt 6.8.3, logical DPI 96, device-pixel ratio 1.00,
window 1200x760. CI debugging history (three fix rounds, runner/workflow
only): buffered ctest output lost on a native hang → direct bounded binary
invocations with streamed logs (300s kill, 30s `-functions` platform probe);
then 0xC0000135 STATUS_DLL_NOT_FOUND → the Qt runtime dir is derived from the
build-tree `Qt6_DIR` cache entry and prepended to the child PATH; then a
Windows appearance-restore `TypeError` (plain mode string vs macOS dict) →
shape-correct save→restore round-trip.

Evidence record `evidence.json` SHA-256:
`c8409c3f938eb35af12b1c30c40b7c1b030fac4ab67d1d628a1632cafe44c4d3`

| Cell id | Views captured | Requested → effective | DPR | Result | Record reference (record SHA-256) |
|---|---|---|---|---|---|
| win-light-100 | 7/7 (default, blocks, huffman, decode-trace, narrow-inspector, focus, stored) | light → light | 1.00 | PASS | `2824ef9bc0a8bac21168719d52f71c2387ec3f1f085ac0c5150f942daab6facc` |
| win-system-light-100 | 7/7 (same views; OS Light flip, fresh process) | system → light | 1.00 | PASS | `b2f83e99c07d3aeb49564c4400f361a3330f9e21d637e95ef2318b4a7717c3d1` |
| win-dark-100 | 7/7 (same views) | dark → dark | 1.00 | PASS | `67d76df33f3775e065e553aa4d1ac9526b064b8eda4f6f2c6796fe0971917e79` |
| win-system-dark-100 | 7/7 (same views; OS Dark flip, fresh process) | system → dark | 1.00 | PASS | `2b516162fdf15760fc487a06735e3642cdfcf6c7a3e68a245176ace83a62a800` |

Appearance restore: pre-run state `light` was saved before the first System
flip and re-applied after the run (readback equals saved state;
`evidence.json` `appearance.restored=true`). Every record passed the runner's
R2 field/schema/fixture-SHA/PNG-SHA validation.

Automated/manual split (R3): the four theme cells above are the Windows
automated units at 100% scale. The 150%/200% scale cells and the Windows
half of the §Manual checks interactive items remain manual units for user
Windows hardware (review checklist below).

## Review checklist (near-final)

Automated cells — all 8 PASS; evidence-record SHA-256 per cell:

| Cell id | Platform / scale | Result | Record SHA-256 |
|---|---|---|---|
| mac-light-retina | macOS arm64, native Retina (DPR 2.00) | PASS | `3871305b257918e108ed1a59083c243f1f287474f98bd070f9850123db9521c1` |
| mac-system-light-retina | macOS arm64, native Retina | PASS | `5b2de5371c0755effdfd8fa9abacef7849ac43e02d3237e09fb469eeee0688a0` |
| mac-dark-retina | macOS arm64, native Retina | PASS | `f2e5e886d2c62f3d9aa842f26e576ebf32002bd229193f224a7749b8b2218491` |
| mac-system-dark-retina | macOS arm64, native Retina | PASS | `269fc1679f98301bdd2cd124580b568f543facc0a2d2fbae4fc76e66d1ab1a42` |
| win-light-100 | Windows x64, 100% | PASS | `2824ef9bc0a8bac21168719d52f71c2387ec3f1f085ac0c5150f942daab6facc` |
| win-system-light-100 | Windows x64, 100% | PASS | `b2f83e99c07d3aeb49564c4400f361a3330f9e21d637e95ef2318b4a7717c3d1` |
| win-dark-100 | Windows x64, 100% | PASS | `67d76df33f3775e065e553aa4d1ac9526b064b8eda4f6f2c6796fe0971917e79` |
| win-system-dark-100 | Windows x64, 100% | PASS | `2b516162fdf15760fc487a06735e3642cdfcf6c7a3e68a245176ace83a62a800` |

Consolidated evidence records: macOS `evidence.json`
`c1febc87c91d891232f372bf734c81603349d233ac7721507e2bdc9df3e618b6`
(commit `a125a5d`), Windows `evidence.json`
`c8409c3f938eb35af12b1c30c40b7c1b030fac4ab67d1d628a1632cafe44c4d3`
(commit `730a8cca`).

Manual units — all **PENDING-HUMAN** (the product owner executes them and
records PASS / issue / BLOCKED-with-reason per the package completion
definition; they are NOT recorded PASS here):

| Unit | Platform | Procedure | Owner | Status |
|---|---|---|---|---|
| mac-light-scaled | macOS arm64 | One logical scaled case: switch the display to a non-default scaled logical resolution (System Settings → Displays → "More Space"/"Larger Text"), relaunch the app, visually verify the same 7 views against the Retina captures; no clipped controls, layout intact | product owner | PENDING-HUMAN |
| win-light-150, win-system-light-150, win-dark-150, win-system-dark-150 | user Windows hardware | Set display scale 150% (Settings → System → Display), launch the app, verify the same 7 views per mode; per-cell PASS/issue notes | product owner | PENDING-HUMAN |
| win-light-200, win-system-light-200, win-dark-200, win-system-dark-200 | user Windows hardware | Same procedure at display scale 200% | product owner | PENDING-HUMAN |
| Interactive M1: selected tabs use at least two cues and remain visible in inactive windows | macOS + Windows | Open a file with several tabs, deactivate the window, verify two-plus cues on the selected tab in both themes | product owner | PENDING-HUMAN |
| Interactive M2: hover, focus, pressed, disabled, error, Current and Selection states are distinct | macOS + Windows | Exercise every state on tabs/buttons/bit-string rows in Light, Dark and System; screenshots per state | product owner | PENDING-HUMAN |
| Interactive M3: no label/button/bit string is clipped at approved widths | macOS + Windows | Resize the Inspector to the approved minimum (360 logical width) and window to minimum width; check all labels/buttons/bit strings | product owner | PENDING-HUMAN |
| Interactive M4: fixed-pitch data remains readable and focus rings are not overwritten | macOS + Windows | Keyboard-navigate the hex/bit-string areas; verify monospace rendering and visible focus rings | product owner | PENDING-HUMAN |
| Interactive M5: System mode reacts to an OS color-scheme change without reopening the file, resetting selection or moving docks | macOS + Windows | Open a file with a selection, flip the OS appearance live (System mode active), verify immediate re-theme with document/selection/dock layout intact | product owner | PENDING-HUMAN |
| Interactive M6: Restart persists explicit Light/Dark; Reset Layout does not reset theme | macOS + Windows | Set Light, restart, verify persisted; repeat for Dark; run Reset Layout and verify theme unchanged | product owner | PENDING-HUMAN |

Manual units outstanding: 21 platform-units (1 macOS scaled cell + 8 Windows
scale cells + 6 interactive items x 2 platforms), all PENDING-HUMAN. A unit
whose hardware is unavailable stays BLOCKED with an explicit reason (package
rule); zero unexplained visual differences must be stated once all units are
executed.

## Verification

```text
cmake --build --preset dev --target pnga_analyzer_gui pnga_gui_application_theme_tests --parallel 4
ctest --preset dev -R 'application_theme|main_window|cross_platform_gui|compression_inspector' --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_qt_package_smoke.py
```

## Completion definition

`PASS` requires every matrix cell, a reviewer checklist and zero unexplained
visual differences. Missing access to one native platform is `BLOCKED`, not
`NOT_CONFIGURED` PASS. A confirmed product defect makes this package `FAIL`
until fixed and recaptured.
