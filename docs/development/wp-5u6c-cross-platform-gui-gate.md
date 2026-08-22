# WP-5U6C — Cross-platform GUI Gate

Status: **implemented locally** (2026-08-23)

## Repeatable checks

`gui_cross_platform_gate_tests` runs without opening a file or invoking a
decoder. It records the host, CPU, Qt version, screen device-pixel ratio and
logical DPI, then verifies:

- the 8 Inspector tabs and Preview/Hex splitter remain visible at 900×600 and
  1600×1000 reference sizes;
- positive control metrics under the detected DPI and font;
- a dark-to-light palette switch without hiding the Inspector;
- the platform-standard `File → Open…` shortcut;
- the explicit coordinate-toolbar → Inspector focus chain;
- non-empty accessible names for coordinate controls and every Inspector page;
- Block, Huffman and Decode Trace truncation rows at their bounded caps.

The test is intentionally invariant-based: it does not assert a platform's
native font, palette or DPI value. This keeps the gate useful on high-DPI and
accessibility configurations while still detecting collapsed layouts.

## Current evidence and coverage

Observed local runner:

```text
macOS Tahoe 26.6.2, arm64, Qt 6.11.1, offscreen platform
screen_dpr=1, logical_dpi=96
```

The gate passed 7/7 cases locally. The full dev and ASan suites each pass
30/30 after adding this test; repository-layout and dependency audits remain
clean. Windows and Linux native window-system runs are **not covered by this
local evidence** and remain release/CI matrix work. The offscreen plugin also
does not claim native compositor, menu styling or screen-reader certification.

Run the focused gate with:

```text
QT_QPA_PLATFORM=offscreen build/dev/tests/gui/pnga_gui_cross_platform_gate_tests
```

