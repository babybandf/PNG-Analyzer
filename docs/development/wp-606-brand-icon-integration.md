# WP-606 — Application Brand Icon Integration

Status: **implemented** (2026-08-23)

## Goal

Apply the approved PNG Analyzer icon assets consistently to the Qt application,
native executable/bundle, installers, Linux desktop integration, and About
dialog without changing the confirmed artwork.

## Inputs and invariants

- The vector master is `ui/qt/resources/branding/png-analyzer-icon.svg`.
- The approved source image is
  `ui/qt/resources/branding/png-analyzer-icon-reference.png`; it is reference
  material only and must not be used directly at runtime.
- The About/README lockup is
  `ui/qt/resources/branding/png-analyzer-lockup.svg` with a committed PNG
  rendering beside it.
- Runtime PNGs at 16, 24, 32, 48, 64, 128, 256, 512, and 1024 px are already
  committed under `ui/qt/resources/icons/png/`.
- Windows, macOS, and Linux package assets are already committed under
  `packaging/icons/`.
- Keep all supplied colors, geometry, proportions, transparent exterior, and
  filenames unchanged. Do not redraw or regenerate the approved artwork.
- Qt remains confined to `ui/qt/`, GUI application code, GUI tests, and
  packaging helpers. Nothing under `libs/` may change.

## Required implementation

1. Add the runtime icon/lockup to the Qt Resource System with stable `:/pnga/`
   aliases and make the resources available both to the GUI and GUI tests.
2. Set the application/window icon before constructing `MainWindow`, so the
   window chrome, taskbar, dock, and application switcher use the brand icon
   where the platform permits it.
3. Show a restrained branded visual in `AboutDialog` while preserving all
   current selectable version, URL, and email content and link behavior.
4. Give the Windows GUI executable the committed `.ico` through a Windows
   resource file and use it for NSIS installer/uninstaller branding where CPack
   supports it.
5. Set the macOS bundle icon to the committed `.icns`, copy it into the bundle
   Resources directory, and keep `macdeployqt`/DMG behavior working.
6. Install the hicolor PNG set on Linux. Add or update a desktop entry only if
   needed for the installed GUI to resolve `Icon=png-analyzer`.
7. Keep CLI-only and Qt-not-configured builds working; icon integration must not
   make Qt mandatory.

## Allowed paths

- `apps/png-analyzer-gui/**`
- `ui/qt/CMakeLists.txt`
- `ui/qt/include/pnga/ui/qt/about_dialog.h`
- `ui/qt/src/about_dialog.cpp`
- `ui/qt/resources/**`
- `tests/gui/**`
- `packaging/**`
- `scripts/run_qt_package_smoke.py`
- `scripts/run_native_package_smoke.py`
- `scripts/run_linux_package_smoke.py`
- `CMakeLists.txt`
- this Work Package document

## Forbidden scope

- No edits under `libs/`, `third_party/`, or corpus fixtures.
- No new dependency, package manager, artwork variant, splash screen, signing,
  notarization, MSIX, AppImage, or Flatpak work.
- Do not modify generated build output or weaken an existing test/smoke gate.

## Acceptance criteria

- `QApplication::windowIcon()` and a constructed `MainWindow::windowIcon()` are
  non-null after startup setup.
- `AboutDialog` exposes a visible, non-null branded pixmap while retaining all
  existing tested text and links.
- Windows target metadata embeds `packaging/icons/png-analyzer.ico`.
- macOS bundle metadata names `png-analyzer.icns` and installs that file in the
  bundle Resources directory.
- Linux install rules place every supplied hicolor size in the matching
  `${CMAKE_INSTALL_DATADIR}/icons/hicolor/<size>x<size>/apps/` directory.
- Qt-free configure/build and existing portable package behavior remain valid.
- No resource path depends on a developer-machine absolute path.

## Verification

Run the cheapest discriminating checks first, then the applicable package gates:

```text
cmake --build --preset dev --target pnga_analyzer_gui pnga_gui_tests
ctest --preset dev -R 'about|main_window|cross_platform_gui' --output-on-failure
python3 scripts/run_qt_package_smoke.py
python3 scripts/run_linux_package_smoke.py
python3 scripts/run_native_package_smoke.py
python3 scripts/run_package_smoke.py
```

Platform-specific gates may report their existing documented `NOT_CONFIGURED`
state when the host lacks the required Qt or installer tooling. Report exactly
one final status (`PASS`, `BLOCKED`, or `FAIL`) with the commands and evidence.
