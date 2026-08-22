# WP-605A packaging

The top-level CMake project installs the self-contained `pnga` CLI, the
optional Qt GUI target and the project license/readme. CPack emits a
relocatable archive:

- Windows: ZIP
- macOS/Linux: compressed TAR archive

This is the first cross-platform install/start smoke boundary. Native DMG,
MSIX, AppImage/Flatpak and Qt framework deployment remain release follow-up
work; the archive must not claim those installer formats.

Run the package smoke from the repository root:

```text
python3 scripts/run_package_smoke.py
```

The generated package and extraction directory stay under `build/` or a
temporary directory and are never added to `tests/corpus`.
