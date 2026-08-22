# WP-600C — Validation Integration

Status: **implemented** (2026-08-23)

## Single composition path

`pnga::analysis_engine::validate_document` is the only application-facing
composition point. It executes structural, WP-600A integrity, WP-600B
semantic/resource and zlib preflight rules in a fixed order and returns the
immutable Qt-free issue report.

- The CLI `validate --json` now emits deterministic `rule_id`, `severity`,
  `message`, physical/logical `offset` and `spec_ref` fields, with exit code 3
  for validation issues and 2 reserved for format scan failures.
- MainWindow starts the same validation bundle on a worker thread after a file
  is indexed. A generation check prevents stale reports from replacing the
  current document. The status bar exposes the issue count, first navigable
  offset and a deterministic tooltip containing all issue ids/messages.
- GUI code does not parse PNG or link `pnga_validation` directly; both clients
  consume the analysis-engine entry point.

## Evidence

CLI goldens cover clean and trailing-byte files, while the GUI gate checks the
accessible `validationStatus` control. Existing structural, integrity,
semantic/decode/resource, dev and ASan suites remain green.

