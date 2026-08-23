# WP-605A packaging

The top-level CMake project installs the self-contained `pnga` CLI, the
optional Qt GUI target and the project license/readme. CPack emits a
relocatable archive:

- Windows: ZIP
- macOS/Linux: compressed TAR archive

This is the first cross-platform install/start smoke boundary. Native DMG,
MSIX, AppImage/Flatpak and Qt framework deployment remain release follow-up
work; the archive must not claim those installer formats. On Linux, the
WP-605D gate additionally builds a Debian package and verifies isolated
install/uninstall without touching the host package database.

Run the package smoke from the repository root:

```text
python3 scripts/run_package_smoke.py
```

Linux native package gate:

```text
python3 scripts/run_linux_package_smoke.py
```

macOS/Windows native installer gate:

```text
python3 scripts/run_native_package_smoke.py
```

The native gate currently validates the CLI DMG/NSIS package only. It does
not claim Qt framework deployment, GUI launch, signing or notarization.

When an official Qt kit and the platform deployment tool are available, the
optional GUI framework/launch gate is:

```text
python3 scripts/run_qt_package_smoke.py
```

It deploys Qt with `macdeployqt` or `windeployqt`, packages into an isolated
temporary location and launches the installed GUI with the offscreen QPA
plugin. Without the required Qt kit the command reports `NOT_CONFIGURED`; it
does not claim a GUI result. Signing, notarization and MSIX remain separate
release gates.

The generated package and extraction directory stay under `build/` or a
temporary directory and are never added to `tests/corpus`. CI uploads the
portable package as `portable-package-Windows` (ZIP) and equivalent TGZ
artifacts for macOS/Linux, so it can be downloaded from the Actions run.
