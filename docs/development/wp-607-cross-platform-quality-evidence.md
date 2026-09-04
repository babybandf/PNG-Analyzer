# WP-607 — Cross-platform Quality Evidence

Status: **design approved; WP-607A frozen for implementation; WP-607C PASS**
(2026-09-04)

## Goal

Close remaining static-product evidence gaps with a provenance-controlled
acceptance corpus, native GUI/accessibility checks, Windows/macOS performance
baselines and a single auditable manifest.

## Dependencies

- WP-5U15: PASS before harness integration.
- WP-607C corpus may be built in parallel with WP-5U12A–E.
- WP-5U12F and WP-5U14N must pass before native final capture.

## Global evidence schema

Every JSON record contains `schema_version`, Work Package, status, commit, UTC
time, OS/build, architecture, compiler, Qt, display protocol, logical DPI,
device-pixel ratio, CPU, memory, corpus revision, command and artifact SHA-256.
Allowed final status is `PASS`, `BLOCKED` or `FAIL`. `NOT_CONFIGURED` remains a
tool result and cannot satisfy a required matrix cell.

## WP-607C — Controlled static UI/Trace corpus

Detailed written package:
`docs/development/wp-607c-controlled-static-ui-trace-corpus.md`.

Status: **PASS** (2026-09-04). Aggregate corpus revision
`5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`;
evidence record `build/evidence/wp-607c-corpus.json` (not committed), SHA-256
`8498e1b45f6d99916b88b1bdb85b389ac83e6510981c304e28df26e84182c1c2`; full suite
52/52 CTest entries; double generation byte-identical; fresh-build rerun
identical. WP-5U12F must re-run `scripts/run_wp607c_corpus_gate.py` as its
first preflight step. WP-607A/B/D and overall WP-607 remain incomplete.

Create deterministic generators and manifest entries for:

- grayscale 1-bit, indexed 4-bit + PLTE/tRNS, RGB8 and RGBA16;
- all five filters, Adam7 including empty passes and 16-bit byte selection;
- Stored, Fixed and Dynamic DEFLATE, literal, non-overlap/overlap match;
- multi-Block, BFINAL, cross-IDAT header/token/adler boundaries;
- truncated stream, reserved BTYPE, invalid distance, CRC and Adler mismatch;
- narrow/large UI and performance fixtures.

Generated fixtures record generator arguments and expected facts. External
fixtures additionally require upstream URL, license and SHA-256. No fixture is
committed without `tests/corpus/manifest.yaml` metadata and at least one test.

## WP-607A — Native GUI and accessibility

Detailed written package:
`docs/development/wp-607a-native-gui-accessibility.md`.

Status: **approved; frozen for implementation** (2026-09-04). Binding review:
`docs/development/wp-607a-written-package-review.md`; implementation plan:
`docs/superpowers/plans/2026-09-04-wp-607a-native-gui-accessibility.md`.

Run on Windows stable x64, macOS stable arm64 and Ubuntu LTS x86_64 using native
window systems. Record File Open, `.png` drag/drop, menus/shortcuts, dock drag/
float/reset, 100/150/200% scale where supported, keyboard-only core workflows,
focus order, accessible names/roles, screen-reader announcements, clipboard and
close/reopen/rapid-switch behavior. Linux records X11 or Wayland explicitly.

Core workflows are Chunk→File bytes, Stage→Pixel, Pixel→Token→compressed bits,
Statistics export once WP-602H passes, and APNG timeline once WP-706 passes.
Static-v1 closure may record later workflows as `out_of_scope`, never PASS.

## WP-607B — Native performance baselines

On Windows and macOS, use the frozen WP-604 corpus and record at least five cold
and twenty warm samples for index, first preview, random row P50/P95, committed
selection P50/P95, bounded Trace P50/P95, cache reopen and peak RSS. Background
load is documented. Fixed thresholds live in a reviewed JSON file; a regression
requires a code fix or independent approval, not an unreviewed baseline update.

## WP-607D — Evidence audit

Verify all artifact hashes, matrix coverage, machine records, fixture manifest
links and command exit statuses. Produce one summary that names missing cells,
distinguishes automated/native/manual results and states exactly which platform
claims are supported. The audit must not infer untested OS support.

## Allowed paths

- `tests/corpus/**`, `tests/gui/**`, `tests/performance/**`
- focused `scripts/**`, `.github/workflows/**`, evidence documentation
- production paths only for a separately reproduced and tested defect

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --build --preset dev --parallel 4
ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev
python3 scripts/run_release_candidate_audit.py
git diff --check
```

## Completion definition

`PASS` requires corpus provenance, all required native cells, accepted
performance thresholds and a hash-valid audit summary. Missing external access
after all local work is complete is `BLOCKED`. Failed or inconsistent evidence
is `FAIL`.
