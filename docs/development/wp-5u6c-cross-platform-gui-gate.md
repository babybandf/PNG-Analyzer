# WP-5U6C — Cross-platform GUI Gate

Status: **implemented locally** (2026-08-23)

## Repeatable checks

`gui_cross_platform_gate_tests` and its `dpi_150`/`dpi_200` registrations run
without opening a file or invoking a decoder. The companion
`scripts/run_gui_gate.py` also replays the executable directly at each scale
factor and records the host, CPU, Qt version, screen device-pixel ratio and
logical DPI, then verifies:

- the 8 Inspector tabs and Preview/Hex splitter remain visible at 900×600 and
  1600×1000 reference sizes;
- positive control metrics under the detected DPI and font, including explicit
  Qt scale factors 1.5 and 2.0;
- a dark-to-light palette switch without hiding the Inspector;
- the platform-standard `File → Open…` shortcut;
- the explicit coordinate-toolbar → Inspector focus chain;
- non-empty accessible names for every Inspector page, including the four
  not-applicable placeholders;
- Block, Huffman and Decode Trace truncation rows at their bounded caps, with
  the literal `truncated` sentinel checked.

The test is intentionally invariant-based: it does not assert a platform's
native font, palette or DPI value. This keeps the gate useful on high-DPI and
accessibility configurations while still detecting collapsed layouts.

## Current evidence and coverage

Observed local runner and generated evidence (`build/gui-gate/wp-5u6c-evidence.json`):

```text
macOS Tahoe 26.6.2, arm64, Qt 6.11.1, offscreen platform
screen_dpr=1, logical_dpi=96
scale_150_screen_dpr=1.5, scale_200_screen_dpr=2
```

All three CTest registrations passed 7/7 QtTest cases locally. The full dev and
ASan suites each passed 34/34 tests. The offscreen plugin does not claim native
compositor, menu styling or screen-reader certification. Windows and Linux
native window-system runs are **not covered by this local evidence** and remain
release/CI matrix work.

Run the focused gate with:

```text
python3 scripts/run_gui_gate.py
```
