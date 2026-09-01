# WP-5U15 — MainWindow Behavior-Preserving Decomposition

Status: **design approved; pending written-package review** (2026-09-01)

## Goal

Reduce `apps/png-analyzer-gui/src/main_window.cpp` from 2,008 lines and
`main_window.h` from 318 lines into focused, independently testable units before
Statistics or APNG adds new application state. This Work Package is a pure
refactor: every user-visible and asynchronous behavior remains unchanged.

## Dependencies

- WP-5U13 Trace Pipeline Application Integration: PASS.
- WP-5U14 Application Theme: implemented.
- Current `main` GUI, Trace and package tests must pass before extraction.

This package precedes WP-5U12 completion, WP-602B re-entry and WP-699–706.

## Non-goals

- No Compression, Statistics, APNG, parser, decoder or UI feature change.
- No new thread model, scheduler, cache, budget or public `libs/**` API.
- No visual redesign, settings migration, action rename or shortcut change.
- No general dependency-injection framework and no catch-all `controller` API.

## Allowed paths

- `apps/png-analyzer-gui/src/main_window.{h,cpp}`
- new focused files under `apps/png-analyzer-gui/src/` named below
- `apps/png-analyzer-gui/CMakeLists.txt`
- `tests/gui/main_window_layout_test.cpp`
- `tests/gui/trace_pipeline_integration_test.cpp`
- `tests/gui/cross_platform_gui_gate_test.cpp`
- `tests/gui/CMakeLists.txt`
- this document

## Forbidden paths

- `libs/**`, `ui/qt/**`, `third_party/**`, packaging and corpus files
- existing settings keys, widget `objectName` values and user-visible strings
- removal, weakening or timing-based relaxation of an existing test

If an extraction requires a `libs/**` public API change or observable behavior
change, return `BLOCKED` and propose a separate Work Package.

## Target file ownership

| File | Single responsibility |
|---|---|
| `document_workers.{h,cpp}` | Decode, Stage, Validation and Chunk Detail QThread implementations |
| `document_session.{h,cpp}` | current source/index/results, generation and worker publication |
| `main_window_ui.{h,cpp}` | build menus, docks, splitters, tabs and return typed widget handles |
| `workspace_controller.{h,cpp}` | QSettings, recent files, layout restore/save/reset |
| `selection_navigation_controller.{h,cpp}` | X/Y Lock, SelectionBus, Chunk/Pixel/Hex state and loop suppression |
| `trace_controller.{h,cpp}` | TraceOrchestrator, task handle, state machine, binding and cancellation |
| `main_window.{h,cpp}` | QMainWindow events, component construction and high-level wiring only |

Do not introduce a source file named `helpers`, `common`, `misc`, `manager` or
`utils`. A new source should normally remain under 500 lines. Final
`main_window.cpp` must be at most 600 lines and `main_window.h` at most 160.

## Required interfaces

### MainWindowUi

Return a non-owning `MainWindowWidgets` aggregate containing every widget and
action used by controllers. `MainWindow` remains the QObject parent/owner.
Construction must preserve creation order, object names, tab order, menu text,
shortcuts, dock areas, default sizes and accessibility names.

### DocumentSession

Own the current source, Chunk index, immutable StageSet, validation report,
generation counter and worker pointers. Expose signals carrying generation and
immutable results. Replacement and close increment generation before clearing
state. A result whose generation differs from the current generation is
discarded before any controller or widget sees it.

### WorkspaceController

Receive explicit widget/action references; do not find arbitrary children by
text. Preserve every current organization/application name and settings key,
including recent-file limits, default 1200×760 size, 900×600 minimum,
Preview/Hex splitter ratio and Reset Layout behavior.

### SelectionNavigationController

Own `SelectionViewState` and navigation-only transient state. Preserve existing
origin IDs, merge behavior, Hex source semantics, locked coordinate keyboard
behavior, Chunk highlight restoration and no-replay hover rule.

### TraceController

Own exactly one TraceOrchestrator per document generation, the existing
4096-token and 8 MiB output budgets, deduplication interval and task
cancellation. Worker callbacks reach the GUI thread through queued invocation.
No page switch, resize, hover or numeric-base change may submit a replay.

## Ordered implementation slices

1. **Baseline contract tests.** Add assertions only where current invariants
   are not already observable: object names, settings keys, worker stale drop,
   duplicate trace suppression and close/open reset.
2. **Workers.** Move worker types without changing constructors, signals,
   result copying or ownership.
3. **UI and workspace.** Extract UI construction, layout and settings. Run the
   full GUI gate before proceeding.
4. **DocumentSession.** Move source/index/result and worker lifecycle while
   retaining generation ordering.
5. **Selection navigation.** Move coordinate, Chunk and Hex wiring with exact
   signal payloads.
6. **TraceController.** Move bounded Trace state and publication.
7. **Facade cleanup.** Remove obsolete includes/state and enforce line gates.

Each slice is a separate commit and must leave the full GUI suite passing.

## Cheapest discriminating test

Before changing code:

```text
cmake --build --preset dev --target pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'main_window_layout|trace_pipeline_integration' --output-on-failure
```

After every slice, the same command must pass with identical test counts.

## Full verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_package_smoke.py --preset release --jobs 2
wc -l apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/src/main_window.h
git diff --check
git status --short
```

## Completion definition

Report `PASS` only when:

- all seven target responsibilities have a single owner;
- MainWindow meets the line gates without generated/catch-all indirection;
- all prior GUI, Trace, settings, drag/drop and package behavior passes;
- static runtime behavior and performance budgets are unchanged;
- changed paths are limited to allowed paths; and
- the report lists commands, test counts and final line counts.

Report `BLOCKED` for an unavoidable public API/behavior change. Report `FAIL`
for incomplete extraction or failed verification.
