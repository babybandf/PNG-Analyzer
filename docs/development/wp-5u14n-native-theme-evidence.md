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
