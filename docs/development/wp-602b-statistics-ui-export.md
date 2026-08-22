# WP-602B — Statistics UI & Export

Status: **deferred from the first single-file v1 surface** (2026-08-23)

## Decision

Do not add a Statistics tab, menu entry, dashboard, JSON/CSV command or
selection-navigation affordance to the current v1 UI. The frozen WP-5U0
contract and user guide explicitly keep Statistics out of the first single-file
workflow, and adding a visible entry now would create an unsupported product
surface rather than close a required Gate.

The completed `pnga_statistics` engine and `pnga_analysis_engine` adapter remain
available as an optional Qt-free capability. They do not change current GUI
state, CLI output schemas or user-facing navigation. Existing deterministic
validation/inspect JSON contracts remain unchanged.

## Re-entry criteria

Reopen WP-602B only with a new scope approval that supplies:

- a stable JSON/CSV schema and locale-independent number formatting;
- explicit ownership of selection-to-statistics navigation and cancellation;
- fixed large-file budgets and a performance corpus;
- a UI/CLI acceptance matrix that does not add Compare or APNG semantics.

Until then, Statistics is an engine-level opt-in and not a v1 acceptance item.
