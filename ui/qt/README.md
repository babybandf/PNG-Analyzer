# pnga_ui_qt

Qt UI library (REPOSITORY_LAYOUT.md §4, §8.1). The only Qt-bearing library;
everything under `libs/` stays Qt-free (ADR-0003).

## Responsibility

- Qt models, widgets, panels, canvas and commands that consume backend-neutral
  analysis models.
- WP-104: the Chunk tree model and the windowed Hex view.

## Non-goals

- Parsing PNG data, Inflate or accessing libpng private structures.
- Blocking the UI event loop with file reads or decoding (AGENTS.md).

## Public targets

- `pnga_ui_qt` (alias `pnga::ui_qt`).

## Allowed dependencies

- Approved Qt modules, `pnga_analysis_engine`, `pnga_trace_model`,
  `pnga_rendering` (layout §7 final state).
- Interim (WP-104, until M2's analysis engine lands): `pnga::png_format`,
  `pnga::io`. This deviation is temporary and must be reconciled at M2.
