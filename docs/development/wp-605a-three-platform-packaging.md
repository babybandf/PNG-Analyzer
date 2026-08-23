# WP-605A — Three-platform Packaging

Status: **implemented as portable archive smoke** (2026-08-23)

The CMake project now installs the self-contained `pnga` CLI, the optional GUI
target and license/readme assets. CPack selects ZIP on Windows and TGZ on
macOS/Linux. `scripts/run_package_smoke.py` configures the release preset when
needed, then builds the release CLI (and GUI
when configured), creates exactly one archive, extracts it into a temporary
directory, checks the packaged LICENSE and runs `pnga --version`.

Run the smoke with:

```text
python3 scripts/run_package_smoke.py
```

The GitHub CI matrix runs the same command on Linux, Windows and macOS after
the dev tests. Qt-free CI packages the CLI only; a Qt-enabled build packages
the GUI binary as well. The smoke does not claim native DMG, MSIX,
AppImage/Flatpak or Qt framework deployment—those are release follow-up work.

Local evidence: macOS arm64, CPack TGZ, archive extraction and `pnga 0.1.0`
startup/version smoke passed. Dev and ASan suites remain 32/32.
