# WP-5U14N Native Windows/macOS Theme Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce reviewable native Windows/macOS screenshots and interaction evidence for every frozen WP-5U14N matrix cell, with zero production-code changes.

**Architecture:** One test-only Qt capture target renders the real `MainWindow` under the real platform theme engine, walks the frozen views per `ApplicationTheme::ThemeMode`, and writes `QWidget::grab()` PNGs plus `pnga-wp5u14n-native-capture-v1` records under `build/evidence/wp-5u14n/`. A Python runner orchestrates platforms/modes (including OS appearance flips between fresh launches), assembles the evidence record, and refuses offscreen runs. Windows theme cells run on a `workflow_dispatch` CI job; scale and interactive cells are explicit manual units executed by the product owner.

**Tech Stack:** C++20, Qt 6.8+ Widgets/Test (native platform plugin), CMake/CTest, Python 3.11+, GitHub Actions `windows-latest`/`macos-latest`.

**Spec:** `docs/development/wp-5u14n-native-theme-evidence.md` (frozen) and `docs/development/wp-5u14n-written-package-review.md` (rulings R1–R7).

## Global Constraints

- No production changes (`libs/`, `ui/qt/`, `apps/`): a revealed defect becomes a separately committed fix with a focused failing test first, or the cell is recorded FAIL and the package FAIL.
- The capture target runs ONLY on the native platform; under `QT_QPA_PLATFORM=offscreen` every case reports skip so the regular dev suite stays 100% green and deterministic (R7).
- Captures use real windows: never set `QT_QPA_PLATFORM=offscreen` for capture runs; never render via offscreen and label it native.
- Evidence lives under `build/evidence/wp-5u14n/` (git-ignored); only the review checklist/evidence document and the capture target/runner sources are tracked.
- Every record carries: OS build, architecture, Qt version, requested + effective `ThemeMode`, logical DPI, device-pixel ratio, window size, git commit, fixture id + SHA-256, capture PNG SHA-256, UTC timestamp, per-cell result (R2).
- Fixtures are pinned WP-607C ids (R2): `ui-rgb8-five-filters`, `ui-gray1-none`, `trace-stored-literals`.
- Missing platform/hardware access for a cell is `BLOCKED`, never `NOT_CONFIGURED` PASS (package completion definition).
- The runner refuses: offscreen platform, missing corpus fixture, dirty capture directory, unknown matrix cell, and any record missing a required field.
- Run tasks serially; each ends with its focused gate and a commit.

## File Structure

| Path | Responsibility |
|---|---|
| `tests/gui/wp_5u14n_native_capture_test.cpp` | Native capture cases: theme modes × views, record emission, offscreen skip |
| `tests/gui/CMakeLists.txt` | Register `pnga_gui_wp_5u14n_native_capture_tests` (R1) |
| `scripts/run_wp_5u14n_capture.py` | Orchestrator: build, per-mode launches (incl. OS appearance flips), record assembly, `--self-test`, refusals |
| `.github/workflows/native-theme-capture.yml` | `workflow_dispatch` Windows/macOS native capture job (R5) |
| `docs/development/wp-5u14n-native-theme-evidence.md` | Final review checklist, matrix results, per-cell dispositions |
| `build/evidence/wp-5u14n/**` | Captures + records (git-ignored, never committed) |

## Required matrix (frozen; automated A / manual M)

| Cell id | Platform | Mode | Scale | Views | Form |
|---|---|---|---|---|---|
| win-light-100 | Windows x64 | Light | 100% | all frozen views | A (CI) |
| win-system-light-100 | Windows x64 | System (OS Light) | 100% | all frozen views | A (CI) |
| win-dark-100 | Windows x64 | Dark | 100% | all frozen views | A (CI) |
| win-system-dark-100 | Windows x64 | System (OS Dark) | 100% | all frozen views | A (CI) |
| win-*-150 / win-*-200 | Windows x64 | 4 modes | 150%, 200% | all frozen views | M (user hardware) |
| mac-*-retina | macOS arm64 | 4 modes | native Retina (DPR 2) | all frozen views | A (local) |
| mac-light-scaled | macOS arm64 | Light | one logical scaled case | all frozen views | M |
| interactive-M1..M6 | both | — | — | §Manual checks items 1–6 | M |

"frozen views" = default page, Compression Blocks, Huffman, Decode Trace, narrow Inspector, focus state (keyboard focus ring visible on one interactive control).

### Task 1: Capture target and record contract

**Files:**
- Create: `tests/gui/wp_5u14n_native_capture_test.cpp`
- Modify: `tests/gui/CMakeLists.txt`

**Interfaces:**
- Consumes: `ApplicationTheme::ThemeMode` programmatic switching, `MainWindow` real construction, WP-607C corpus dir compile definition pattern (`PNGA_WP607C_CORPUS_DIR` precedent).
- Produces: CTest entries `wp5u14n_capture_<mode>` (Light/System/Dark with OS-mode decorrelation via `PNGA_WP5U14N_OS_MODE`), records + PNGs under the output dir from `PNGA_WP5U14N_OUT`; offscreen → all cases skip.

- [ ] **Step 1: Write the failing contract test (self-asserting)**

The test binary validates its own record contract before capturing: build one record struct per cell and assert every required field is present and non-empty (R2 list), assert `stable_id`/fixture SHA lookup via the corpus registry, and assert the offscreen skip path returns skip results. Run under offscreen; expected RED: types/entry points absent.

- [ ] **Step 2: Register the target (offscreen-skipping) and confirm RED**

CMake registration mirrors `pnga_gui_application_theme_tests` (AUTOMOC, same libs) plus the corpus dir definition; `add_test` per mode with `ENVIRONMENT PNGA_WP5U14N_OUT=<build>/evidence/wp-5u14n` and labels `native-capture;wp5u14n`. Build must succeed; running under offscreen reports skips (not failures). RED = the contract test's missing-symbol compile failure.

- [ ] **Step 3: Implement native capture cases**

For each requested mode: construct `MainWindow`, open `ui-rgb8-five-filters`, set `ThemeMode` programmatically, process events until `effective_mode_` matches, capture in order: default page → Blocks → Huffman → Decode Trace → narrow Inspector (360 logical width) → focus state (Tab to one control, capture with focus ring), via `QWidget::grab()`; write `captures/<cell>-<view>.png` and one JSON record per cell. Open `ui-gray1-none` for the narrow case and `trace-stored-literals` for the Stored view. Skip path (offscreen): write `{"result": "skipped-offscreen"}` per cell, exit 0.

- [ ] **Step 4: Run the full dev suite (offscreen) and commit**

```bash
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
```

Expected: 100% pass, capture entries report skips, tree contains only the two files. Commit: `test: add wp5u14n native capture target`.

### Task 2: Orchestrator runner

**Files:**
- Create: `scripts/run_wp_5u14n_capture.py`

**Interfaces:**
- Produces: `python3 scripts/run_wp_5u14n_capture.py --platform {macos,windows} --preset dev --jobs 4` → per-mode native ctest invocations, OS appearance flips between System cells (macOS: `defaults write -g AppleInterfaceStyle` + `osascript` appearance notification; Windows: registry `AppsUseLightTheme` + `WM_SETTINGCHANGE` broadcast via PowerShell), evidence assembly `build/evidence/wp-5u14n/evidence.json` (schema `pnga-wp5u14n-native-capture-v1`, absolute-path-free, sorted keys), `--self-test` (command plan + record-schema validation), explicit refusals per Global Constraints.

- [ ] **Step 1: Failing self-test for plan + schema**

Implement `--self-test` first asserting the exact command plan for one platform and that a synthetic record missing any required field is rejected. RED: functions absent.

- [ ] **Step 2: Implement the runner; verify native macOS run end-to-end**

Run: `python3 scripts/run_wp_5u14n_capture.py --platform macos --preset dev --jobs 4` from the local Mac. Expected: 4 modes × views captured natively, records complete, appearance restored to its pre-run state (runner must save + restore `AppleInterfaceStyle`), evidence JSON written, no absolute paths.

- [ ] **Step 3: Commit**

`git add scripts/run_wp_5u14n_capture.py && git commit -m "test: add wp5u14n capture orchestrator"`

### Task 3: Execute macOS matrix and review checklist

**Files:**
- Modify: `docs/development/wp-5u14n-native-theme-evidence.md`

- [ ] **Step 1: Run macOS automated cells (A) and assemble per-cell results**
- [ ] **Step 2: Product owner executes M cells (mac scaled case; interactive checks M1–M6 macOS half) with per-cell PASS/issue notes**
- [ ] **Step 3: Write the macOS review checklist section (cell → capture SHA → disposition) and commit** `docs: record wp5u14n macos evidence`

### Task 4: Windows native capture via CI

**Files:**
- Create: `.github/workflows/native-theme-capture.yml`
- Modify: `docs/development/wp-5u14n-native-theme-evidence.md`

- [ ] **Step 1: `workflow_dispatch` job on `windows-latest`**: setup-python + PyYAML pin (ci.yml precedent), MSVC DevShell (ci.yml precedent), build capture target, run runner with `--platform windows`, upload `build/evidence/wp-5u14n/` as artifact. Registry flip for System Dark includes a `WM_SETTINGCHANGE` broadcast and a verification read-back.
- [ ] **Step 2: Dispatch, retrieve artifact, record per-cell results in the evidence doc** (any CI-captured cell that proves a rendering defect → FAIL path per package).
- [ ] **Step 3: Commit** `docs: record wp5u14n windows ci evidence`

### Task 5: Manual units, final checklist, completion

**Files:**
- Modify: `docs/development/wp-5u14n-native-theme-evidence.md`

- [ ] **Step 1: Product owner executes** Windows scale cells (needs Windows hardware) and remaining interactive manual units; unexecutable cells are recorded `BLOCKED` with reason (package rule).
- [ ] **Step 2: Reviewer checklist finalized** — every matrix cell: capture SHA + automated/manual result or BLOCKED; zero unexplained visual differences statement; any defect → separate fix commit + recapture note.
- [ ] **Step 3: Final verification replay**

```bash
cmake --build --preset dev --target pnga_analyzer_gui pnga_gui_application_theme_tests --parallel 4
ctest --preset dev -R 'application_theme|main_window|cross_platform_gui|compression_inspector|wp5u14n' --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_qt_package_smoke.py
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
git diff --check && git status --short
```

- [ ] **Step 4: Set final status** PASS only if every cell is covered (A PASS + M PASS); BLOCKED cells keep the package BLOCKED with explicit boundaries. Commit `docs: close wp5u14n native theme evidence`.

## Completion definition

Frozen package §Completion definition applies verbatim: PASS requires every matrix cell (automated PASS + manual PASS), the reviewer checklist, and zero unexplained visual differences; one native platform missing → BLOCKED; confirmed product defect → FAIL until fixed and recaptured.
