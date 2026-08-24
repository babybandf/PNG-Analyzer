# WP-5U14 — Product Application Theme

Status: **implemented; automated gates passing; native screenshot evidence pending** (2026-08-24)

## 1. Goal

Add one product-owned Qt Widgets theme system that gives PNG Analyzer a clear,
accessible and intentionally consistent visual hierarchy on Windows and macOS.
The theme must stop relying on each platform's default tab, font and panel
styling for product-critical states while preserving native window chrome,
menus where Qt requires them, file dialogs and platform interaction behavior.

The current macOS light appearance is the visual reference for density,
typographic hierarchy, neutral surfaces and prominent blue selected tabs. The
goal is not a pixel-for-pixel macOS imitation on Windows; Windows must reach the
same clarity and polish without copying macOS title bars or platform chrome,
and the themed macOS build must not visibly regress from the current baseline.

This Work Package owns theme infrastructure and visual presentation only. It
must not change PNG parsing, decoding, reconstruction, trace semantics,
selection semantics or asynchronous publication behavior.

## 2. Baseline evidence

The current application has no application-level theme installation:

- `apps/png-analyzer-gui/src/main.cpp` constructs `QApplication` and then
  immediately constructs `MainWindow`; it does not install a `QStyle`, palette,
  application font or application stylesheet.
- `previewTabs`, `inspectorTabs` and `compressionInspectorPages` are plain
  `QTabWidget` instances, so their selected state is drawn entirely by the
  active platform style.
- `HexSourceTabBar` hard-codes the same `#d0d0d0` background for normal,
  selected and hover states; selection is communicated only by bold text.
- `StageInspector`, `StagePixelProcessView` and `HexView` request the platform's
  recommended fixed font, which intentionally resolves to different families,
  sizes and metrics on Windows and macOS.
- `DeliveredImageView`, `HexSourceTabBar`, `MainWindow` separators and semantic
  reconstruction cells each contain independent local color/style decisions.
- The current cross-platform GUI gate checks layout invariants under the
  offscreen platform. It explicitly does not prove native Windows rendering or
  visual quality.

The Windows and macOS release builds may continue to use platform-specific Qt
plugins. That is not a defect; the application theme must create the stable
product layer above those plugins.

## 3. Product invariants

1. The same theme mode and content state must have the same information
   hierarchy on Windows and macOS. Exact glyph rasterization and native window
   chrome are allowed to differ.
2. The macOS Light reference keeps its compact density, rounded blue selected
   tabs, readable text scale and restrained neutral panels within normal Qt
   rasterization tolerance.
3. Selected tabs must be immediately distinguishable from unselected tabs.
   Selection must use at least two cues: color plus weight, border or indicator.
4. Hover, keyboard focus, pressed, disabled and inactive-window states must
   remain distinguishable. A hover effect must not be the only indication of
   keyboard focus.
5. The existing approved brand blue `#2563EB` is the light-theme primary accent.
   Teal `#14B8A6`, amber `#F59E0B` and violet `#8B5CF6` remain secondary brand or
   semantic colors and must not replace the primary navigation accent.
6. Normal text must target a contrast ratio of at least 4.5:1; large text,
   focus indicators and essential control boundaries must target at least 3:1.
7. Theme changes must not recreate analysis models, reopen a file, submit a
   trace request, change selection, reset docks/splitters or change the current
   tab.
8. `View -> Reset Layout` must not reset the theme preference.
9. Theme resources must be embedded through Qt resources and must not depend on
   developer-machine absolute paths.
10. The first implementation must not bundle a third-party font. Font selection
   uses installed platform families plus deterministic fallbacks.
11. Qt remains confined to `ui/qt`, the GUI application, GUI tests and approved
    packaging/automation helpers. Nothing under `libs/` may change.

## 4. Theme modes and persistence

The application must provide three mutually exclusive modes:

- `System`: follow `QStyleHints::colorScheme()` and react to runtime changes.
- `Light`: use the product light palette regardless of system appearance.
- `Dark`: use the product dark palette regardless of system appearance.

The default for a new installation is `System`. Store the explicit preference
under `appearance/theme` using the existing `QSettings` mechanism. Accepted
stable serialized values are `system`, `light` and `dark`; unknown values must
fall back to `system` without failing startup.

Add `View -> Theme -> Follow System | Light | Dark` as an exclusive action
group. Changing the action applies the theme immediately and persists it.

## 5. Architecture

### 5.1 Owner and public surface

Add a focused Qt theme controller under `pnga::ui::qt`:

```text
ui/qt/include/pnga/ui/qt/application_theme.h
ui/qt/src/application_theme.cpp
```

The public surface must expose only the concepts needed by the GUI application
and Qt widgets:

- `ThemeMode { System, Light, Dark }`;
- installation/application of the selected mode;
- the currently requested and effective mode;
- application UI and monospace fonts;
- named semantic colors needed by custom-painted widgets;
- a `themeChanged` notification for widgets that cache rendered content.

Do not expose raw mutable palettes or a general-purpose string token map to
callers. The controller owns `QStyle`, palette, stylesheet and font order.

### 5.2 Installation order

`main.cpp` must create and configure the theme controller after constructing
`QApplication` but before constructing `MainWindow`. The controller must remain
alive for the complete GUI event loop.

Preserve the native macOS application style so the accepted macOS control
metrics and platform integration do not regress. On Windows and Linux, use Qt
`Fusion` as the stable workspace baseline unless native visual evidence proves
an equal or better result under the complete product stylesheet. The product
palette and scoped stylesheet are applied above the chosen base. Native
top-level window chrome, standard shortcuts and native file-dialog behavior
remain platform-owned.

If the requested base style cannot be created, startup must continue with the
active platform style and record a concise diagnostic; a missing optional
visual baseline must not make PNG Analyzer unusable.

### 5.3 Resources and tokens

Add a dedicated Qt resource rather than expanding the branding-only resource:

```text
ui/qt/resources/png-analyzer-ui.qrc
ui/qt/resources/theme/application.qss
```

QSS has no native variables. The controller may expand a closed set of
reviewed placeholders from an immutable light/dark token structure. Loading
must fail safely: a missing resource or unresolved placeholder keeps the
palette and reports a diagnostic rather than installing a partially expanded
stylesheet.

Required token roles include:

- window, base, raised and alternate surfaces;
- primary and muted text;
- subtle and strong borders;
- accent, accent hover, accent pressed and accent text;
- focus ring;
- disabled text/surface;
- current-pixel, dependency and neutral reconstruction states.

Light theme starts from the approved brand family:

| Role | Initial value |
|---|---|
| window | `#F8FAFC` |
| base | `#FFFFFF` |
| raised | `#F1F5F9` |
| text | `#0F172A` |
| muted text | `#475569` |
| border | `#CBD5E1` |
| accent | `#2563EB` |
| accent hover | `#1D4ED8` |
| accent text | `#FFFFFF` |

Dark theme must use a lighter blue accent where required for contrast and must
be validated by tests rather than obtained by mechanically inverting the light
colors.

### 5.4 Typography

The application UI font starts from
`QFontDatabase::systemFont(QFontDatabase::GeneralFont)` so Windows and macOS
retain familiar text shapes. The controller may normalize undersized defaults,
but must preserve OS accessibility scaling and must not force one bundled font.

The application monospace font uses an ordered installed-family query:

- Windows: `Cascadia Mono`, `Consolas`, then system FixedFont;
- macOS: `SF Mono`, `Menlo`, `Monaco`, then system FixedFont;
- Linux: `Noto Sans Mono`, `DejaVu Sans Mono`, then system FixedFont.

The resolved font must be fixed-pitch and use a readable size derived from the
application font. Replace direct FixedFont calls in Inspector, stage-process
text and Hex view with this shared result.

## 6. Required visual behavior

### 6.1 Primary horizontal tabs

Apply a scoped style to `previewTabs`, `inspectorTabs` and
`compressionInspectorPages`:

- unselected: raised neutral surface and muted/primary readable text;
- hover: a distinct neutral hover surface;
- selected: accent surface with accent text and increased weight;
- keyboard focus: visible focus ring independent of hover;
- disabled: muted text and surface without losing label legibility;
- pane: one stable border and base surface with no double border at the tab.

The style must remain usable when scroll buttons appear and at 150%/200% scale.

### 6.2 Hex source vertical tabs

Remove the local hard-coded gray stylesheet. `hexSourceTabs` has its own scoped
contract:

- selected tab uses a base/raised surface, stronger text and a brand accent
  indicator on the content-facing edge;
- hover differs from selected;
- every label remains readable after rotation at supported DPI values;
- selection is visible without relying only on bold weight.

### 6.3 Remaining shell controls

Move local product presentation into the application stylesheet where it can
be scoped safely:

- dock titles and separators;
- table/tree headers and selected rows;
- spin boxes, flat numeric-base button and check box focus state;
- status bar boundaries;
- image zoom control frame, buttons and combo box;
- scroll-area and text/report boundaries.

Do not use an unqualified `QWidget { ... }` rule. Broad selectors must not
break native dialogs, popup menus, check indicators or child surfaces.
Scrollbars may retain the Fusion/platform baseline in this Work Package unless
a tested product rule is required.

### 6.4 Semantic content colors

Current pixel, filter dependency and neutral neighborhood cells carry domain
meaning. Move their colors to named theme semantic roles while preserving all
existing facts and labels. Current and dependency cells must remain
distinguishable in light and dark themes without color being the sole cue.

## 7. Required implementation sequence

1. Add failing theme resource, palette, typography and persistence tests.
2. Implement `ApplicationTheme`, resource loading and safe token expansion.
3. Install the theme before `MainWindow` construction.
4. Add the View/Theme action group and persistence.
5. Migrate primary horizontal tabs and Hex source tabs.
6. Replace direct FixedFont choices with the shared monospace font.
7. Migrate remaining local shell styles and semantic color access.
8. Extend the GUI gate and produce Windows/macOS visual evidence.
9. Run full GUI and repository regression gates.

Each step must leave the GUI buildable. Styling must not be mixed with changes
to PNG/Deflate analysis or inspector data contracts.

## 8. Allowed paths

- `docs/development/wp-5u14-application-theme.md`
- `docs/user-guide.md`
- `apps/png-analyzer-gui/src/main.cpp`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `ui/qt/CMakeLists.txt`
- `ui/qt/README.md`
- `ui/qt/include/pnga/ui/qt/application_theme.h`
- `ui/qt/src/application_theme.cpp`
- `ui/qt/src/delivered_image_view.cpp`
- `ui/qt/src/hex_source_tab_bar.cpp`
- `ui/qt/src/hex_view.cpp`
- `ui/qt/src/stage_inspector.cpp`
- `ui/qt/src/stage_pixel_process_view.cpp`
- `ui/qt/resources/png-analyzer-ui.qrc`
- `ui/qt/resources/theme/application.qss`
- `tests/gui/CMakeLists.txt`
- `tests/gui/application_theme_test.cpp`
- `tests/gui/cross_platform_gui_gate_test.cpp`
- `scripts/run_gui_gate.py`
- `.github/workflows/ci.yml` only if needed to run/upload the approved GUI
  theme evidence; do not rewrite unrelated CI jobs
- this Work Package's generated evidence under `build/gui-gate/` (untracked)

If implementation needs another production path, update and review this Work
Package before editing that path.

## 9. Forbidden scope

- No edits under `libs/`, `third_party/`, `tests/corpus/` or approved branding
  artwork.
- No new GUI framework, package manager, style plugin or third-party theme.
- No Qt Quick/QML migration and no Electron/web frontend.
- No bundled font, copied font file or new font license in this Work Package.
- No custom title bar, frameless window, acrylic/Mica material, animation
  framework or platform-private API.
- No layout redesign, dock replacement, tab reordering or inspector data
  redesign.
- No generated build output committed to the repository.
- No weakening of existing GUI layout, accessibility, package or analysis tests.

## 10. Tests and visual evidence

### 10.1 Automated invariants

Add focused tests proving:

- all three serialized theme values round-trip and an unknown value falls back
  to System;
- Light and Dark install complete palettes and a non-empty stylesheet with no
  unresolved token markers;
- effective System mode follows the reported Qt color scheme;
- resolved UI and monospace fonts are valid and the monospace result is
  fixed-pitch;
- theme switching preserves MainWindow geometry, dock/splitter state, current
  tabs and selection-facing widget state;
- selected and unselected primary tabs render different product states;
- selected Hex source has an accent indicator distinct from hover/unselected;
- light/dark semantic cells remain distinguishable;
- 900x600 and 1600x1000 layouts still pass at 100%, 150% and 200% Qt scale.

Do not assert a whole-window pixel hash. Font rasterization and native chrome
may differ while the product contract remains correct.

### 10.2 Screenshot matrix

Produce reviewable, non-golden screenshots for:

| Platform | Theme | Scale | Reference window |
|---|---|---:|---:|
| Windows | Light, Dark | 100%, 150% | 1200x760 |
| macOS | Light, Dark | native Retina scale | 1200x760 logical |

Each screenshot must show a selected Preview tab, selected Reconstruction tab,
selected Hex source, Chunk table, image zoom controls and populated Inspector.
CI may upload these as evidence, but a screenshot created with the offscreen
plugin must be labeled offscreen and must not be claimed as native rendering.
The macOS Light screenshot must also be reviewed beside the pre-theme macOS
reference to demonstrate that density, hierarchy and overall polish did not
regress.

## 11. Acceptance criteria

- Windows and macOS use the same product token set and workspace hierarchy.
- The macOS Light result remains recognizably close to the accepted pre-theme
  macOS reference while using the approved brand accent and shared tokens.
- `Image`, `Reconstruction` and the current Hex source have unmistakable
  selected states in both Light and Dark modes.
- Windows no longer depends on the native tab style to communicate selection.
- Inspector and Hex text resolve to a readable fixed-pitch font through the
  documented fallback order.
- Theme mode can be changed from View/Theme, persists across restart and is not
  removed by Reset Layout.
- System mode responds to a runtime system color-scheme change without
  resetting analysis or workspace state.
- Existing semantic highlights, keyboard focus, accessibility names and native
  shortcuts remain available.
- No new third-party dependency or font asset is introduced.
- Qt-free builds and release packaging remain functional.
- Windows and macOS screenshot evidence is attached and labeled with OS, Qt
  version, theme, logical DPI and device-pixel ratio.

## 12. Verification

Run the cheapest discriminating checks first:

```text
python3 scripts/verify_repository_layout.py
cmake --preset dev
cmake --build --preset dev --target pnga_analyzer_gui pnga_gui_tests --parallel 4
ctest --preset dev -R 'application_theme|main_window|cross_platform_gui|hex_source|stage_inspector|stage_pixel' --output-on-failure
python3 scripts/run_gui_gate.py
ctest --preset dev --output-on-failure
python3 scripts/run_package_smoke.py
```

On Windows and macOS, additionally run the native screenshot evidence path and
the existing Qt deployment smoke where configured:

```text
python3 scripts/run_qt_package_smoke.py
```

Platform-specific package gates may retain their existing documented
`NOT_CONFIGURED` result when the required installer tooling is absent. Theme
visual acceptance may not be marked complete until native Windows and macOS
evidence has been reviewed.

## 13. Implementation record

The product theme controller, embedded QSS resource, Light/Dark/System
palette and font selection, View/Theme action group, semantic-color migration,
and focused GUI tests are implemented in commit `6f5f1b5`.

Automated evidence collected on macOS:

- `QT_QPA_PLATFORM=offscreen ctest --test-dir build/dev --output-on-failure` — 40/40;
- `python3 scripts/run_gui_gate.py` — default, 150% and 200% GUI gate;
- `python3 scripts/run_package_smoke.py --preset release --jobs 2` — package smoke.

Native Windows/macOS screenshot review remains pending because this workspace
does not provide the required native cross-platform capture environments.

## 14. Completion definition

WP-5U14 is complete only when every acceptance criterion is satisfied, focused
and full GUI tests pass, screenshot evidence covers both target desktop
platforms, and the final report states exactly one status: `PASS`, `BLOCKED` or
`FAIL` with commands and concise evidence.
