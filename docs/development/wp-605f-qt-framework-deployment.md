# WP-605F — Qt Framework Deployment & GUI Launch Smoke

Status: **planned; scope frozen** (2026-08-23)

## Goal

When a Qt 6 kit is available, deploy the GUI's Qt frameworks/plugins into the
native package and prove that the installed GUI can start in an isolated,
offscreen smoke environment. The existing CLI installer and portable archive
gates remain required and unchanged.

## Scope and invariants

- macOS uses a `MACOSX_BUNDLE` target and `macdeployqt`.
- Windows uses `windeployqt` against the installed GUI executable.
- Packaging and launch happen under temporary directories; no developer or
  runner installation is mutated.
- The launch check is bounded and offscreen only; it verifies process startup,
  not GUI behavior or accessibility.
- Qt remains confined to `ui/qt/` and the GUI application. No parser, decoder,
  zlib or libpng code moves into GUI or packaging helpers.

## Non-goals

Signing, notarization, MSIX, Linux desktop packaging, Qt kit provisioning and
full GUI/manual acceptance are separate release work. A missing Qt kit reports
`NOT_CONFIGURED`; it is not a false PASS.

## Cheapest discriminating verification

```text
python3 scripts/run_qt_package_smoke.py
```

The runner must report `PASS` only after deployment and a bounded installed GUI
launch; otherwise it reports `NOT_CONFIGURED` for a missing platform/tool kit or
fails with actionable evidence.
