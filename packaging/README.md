# WP-605A packaging

The top-level CMake project installs the self-contained `pnga` CLI, the
optional Qt GUI target and the project license/readme. CPack emits a
relocatable archive:

- Windows: ZIP
- macOS/Linux: compressed TAR archive

Release archive names use `macOS` for the macOS platform (rather than the
toolchain-level `Darwin` identifier), for example
`png-analyzer-0.1.0-macOS-arm64.tar.gz`.

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
The `release-portable` workflow publishes those same three CPack archives as
assets on an existing `v*` GitHub Release; it never creates or pushes a tag.

## Brand icon assets

- `icons/png-analyzer.ico` brands the Windows GUI executable and NSIS
  installer/uninstaller.
- `icons/png-analyzer.icns` is copied into the macOS application bundle and
  named by its bundle metadata.
- `icons/linux/hicolor/` is installed with `linux/png-analyzer.desktop` when
  the Qt GUI is configured on Linux.
