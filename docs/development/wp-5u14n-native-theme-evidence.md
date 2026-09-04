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

Workflow: `.github/workflows/native-theme-capture.yml` (new; `workflow_dispatch`
only — never triggered by push/PR, so CI minutes stay bounded), job
`windows-capture` on `windows-latest`, 45-minute timeout. It mirrors ci.yml
precedents (setup-python 3.11 + `PyYAML==6.0.2`, official MSVC DevShell with
persisted environment, pinned vcpkg tool checkout + binary caches,
`python scripts/bootstrap.py`, `cmake --preset dev`) and builds the corpus
fixture generator `pnga_generate_wp607c_corpus` plus the R1 capture target
`pnga_gui_wp_5u14n_native_capture_tests`.

GUI/Qt is REQUIRED for this capture, unlike ci.yml: the target renders the real
MainWindow on the real windows platform. The workflow therefore installs pinned
official Qt 6.8.3 binaries (`win64_msvc2022_64`) through the pinned
aqtinstall 3.3.0 downloader and points `CMAKE_PREFIX_PATH` at the kit. The
System Light/Dark flips (registry `AppsUseLightTheme` + `WM_SETTINGCHANGE`
broadcast + verification read-back) are implemented inside the runner, fresh
process per cell. Evidence is uploaded as artifact
`wp5u14n-native-capture-windows` (`if: always()`) from
`build/dev/evidence/wp-5u14n/` plus the ctest log. If the hosted session cannot
render, the target/runner record honest failures; cells are never faked.

Dispatch outcome: **BLOCKED — no run created.** GitHub requires a
`workflow_dispatch` workflow to exist on the default branch before it can be
dispatched against any ref. Commit `ec495f2` pushed the workflow to
`wp-5u14n-native-theme-evidence` only (`main` still lists only
ci/deps-smoke/release-portable); both dispatch attempts failed:

- `gh workflow run native-theme-capture.yml --ref wp-5u14n-native-theme-evidence`
  → `HTTP 404: workflow native-theme-capture.yml not found on the default branch`
- `gh api -X POST .../actions/workflows/native-theme-capture.yml/dispatches
  -f ref=wp-5u14n-native-theme-evidence` → `404 Not Found`

Publishing the workflow to `main` is outside this work package's allowed
paths, so the dispatch was not forced. Unblock step: publish the workflow
commit to the default branch, then re-run
`gh workflow run native-theme-capture.yml --ref wp-5u14n-native-theme-evidence`,
download `wp5u14n-native-capture-windows`, and record the per-cell results in
this section.

| Cell id | Views | Result |
|---|---|---|
| win-light-100 | — | BLOCKED (dispatch unavailable; GitHub requires the workflow on the default branch) |
| win-system-light-100 | — | BLOCKED (same reason) |
| win-dark-100 | — | BLOCKED (same reason) |
| win-system-dark-100 | — | BLOCKED (same reason) |

No Windows capture records or PNG SHA-256 values exist yet; per R5 and the
package completion definition the Windows automated cells stay `BLOCKED` until
the dispatch path executes — they are never recorded as PASS without captures.
The `win-*-150/200` scale cells and the Windows interactive checks M1–M6
remain manual units for user Windows hardware (R3), unaffected by this
blocker.

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
