# WP-605D — Linux Native Package Gate

Status: **implemented; Ubuntu CI install/uninstall smoke passing** (2026-08-23)

This work package adds Debian metadata to the existing CPack install tree and
checks the native package without mutating the runner host. The default CPack
generator remains the WP-605A portable archive; `scripts/run_linux_package_smoke.py`
selects `DEB`, validates package metadata and required files, installs into an
isolated `dpkg --root` database, runs `pnga --version`, and uninstalls it.

Run on Linux with:

```text
python3 scripts/run_linux_package_smoke.py
```

The gate is Linux-only. Native macOS DMG, Windows MSIX, AppImage/Flatpak and
Qt framework deployment remain separate release work and are not claimed here.
