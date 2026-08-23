# WP-605E — macOS/Windows Native Installer Smoke

Status: **implemented; CI verification in progress** (2026-08-23)

The CPack configuration now carries native metadata while preserving TGZ/ZIP
as the default portable generators. `scripts/run_native_package_smoke.py`
selects DragNDrop on macOS and NSIS on Windows, then verifies the CLI in the
native package. macOS mounts the DMG read-only and detaches it; Windows runs a
silent install into a temporary directory and invokes the generated
uninstaller. Neither path mutates a developer or CI host installation.

This gate intentionally covers the CLI package only. Qt framework deployment
(`macdeployqt`/`windeployqt`), GUI launch, signing and notarization are separate
release work and must not be inferred from this smoke.

The existing portable package smoke now uploads its generated archive as a
GitHub Actions artifact. Windows therefore exposes a directly downloadable
portable ZIP, while macOS/Linux expose their TGZ equivalents.

Run on macOS or Windows with:

```text
python3 scripts/run_native_package_smoke.py
```
