# WP-605F — Qt Framework Deployment & GUI Launch Smoke

Status: **implemented; CI gate verified** (2026-08-23)

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

Implementation is in commit `03665dd`. The macOS and Windows CI jobs execute the
runner and upload `qt-deployment-<OS>` evidence; the current hosted runners do
not provide a Qt kit, so both jobs record `NOT_CONFIGURED` without weakening the
build gate. A local macOS Qt 6.11.1 staging install deployed the frameworks and
offscreen plugin and launched the GUI successfully; local DMG creation is
limited by the sandboxed host's unavailable `hdiutil` device service.
