# WP-607A Native GUI and Accessibility Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce validated native GUI, keyboard, lifecycle and screen-reader evidence for the frozen static-v1 workflows on Windows x64, macOS arm64 and Ubuntu 24.04 LTS x86_64 without changing production code.

**Architecture:** A dedicated QtTest binary drives the real `MainWindow` through public interfaces and stable widget identities on a native Qt platform plugin, emitting one record for A01–A11. A Python runner builds the target, materializes the WP-607C corpus, launches bounded native processes, validates `pnga-wp607a-native-gui-v1` records and creates manual templates for M01–M06. Platform raw evidence stays under `build/evidence/wp-607a/`; a tracked summary records dispositions and artifact hashes.

**Tech Stack:** C++20, Qt 6.8+ Widgets/Test/Accessibility, CMake/CTest, Python 3.11+, GitHub Actions `workflow_dispatch`, WP-607C generated corpus.

**Spec:** `docs/development/wp-607a-native-gui-accessibility.md` and binding rulings R1–R10 in `docs/development/wp-607a-written-package-review.md`.

## Global Constraints

- Modify no production path: `libs/**`, `ui/qt/**`, `apps/**`, `third_party/**`, packaging and corpus files are forbidden.
- Native PASS requires Qt platform `windows`, `cocoa`, `xcb` or `wayland`; `offscreen`, `minimal` and Xvfb-only runs skip/refuse and never satisfy a cell.
- Use only the five frozen WP-607C fixture ids and record each fixture SHA plus corpus revision `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`.
- Generated evidence contains no absolute path, username, hostname, clipboard payload, screen-reader audio or other personal data.
- Statistics export and APNG timeline are explicit `out_of_scope`, never PASS.
- A production defect sets the affected cell and WP-607A to FAIL; fix it only through a separately authorized defect package with a focused failing test.
- Run tasks serially; each task ends with its focused gate and commit before the next begins.

## File Structure

| Path | Responsibility |
|---|---|
| `tests/gui/wp_607a_native_gui_gate_test.cpp` | Native A01–A11 driver, QAccessible snapshot and per-cell JSON emission |
| `tests/gui/CMakeLists.txt` | Register `pnga_gui_wp_607a_native_gui_gate_tests` with corpus directory definition |
| `scripts/run_wp_607a_native_gui_gate.py` | Build/launch orchestration, schema and privacy validation, manual-template generation, evidence assembly |
| `.github/workflows/native-gui-accessibility.yml` | Dispatch-only Windows automated run and artifact upload |
| `docs/evidence/wp-607a-native-gui-accessibility.md` | Tracked platform matrix, commands, artifact hashes, reviewer dispositions and final status |
| `build/evidence/wp-607a/<platform-id>/**` | Ignored raw automated/manual records and logs |

## Frozen Cell-to-Implementation Map

| Cell | QtTest slot / execution owner |
|---|---|
| A01 | `openCloseReopen()` |
| A02 | `dragDrop()` |
| A03 | `menuShortcuts()` |
| A04 | `dockFloatReset()` |
| A05 | `keyboardFocus()` |
| A06 | `accessibleTree()` |
| A07 | `clipboard()` |
| A08 | `rapidSwitch()` |
| A09 | `chunkToFileBytes()` |
| A10 | `stageToPixel()` |
| A11 | `pixelToTokenBits()` |
| M01 | Product-owner native File Open and pointer drag/drop checklist |
| M02 | Product-owner keyboard-only workflow and focus-order checklist |
| M03 | Product-owner pointer dock/scale checklist |
| M04 | Product-owner Narrator/VoiceOver/Orca checklist |
| M05 | Product-owner native clipboard checklist |
| M06 | Product-owner lifecycle/stale-state checklist |

### Task 1: Freeze the runner schema and refusal contract

**Files:**
- Create: `scripts/run_wp_607a_native_gui_gate.py`

**Interfaces:**
- Produces: constants `SCHEMA = "pnga-wp607a-native-gui-v1"`, `SCHEMA_VERSION = 1`, `WORK_PACKAGE = "WP-607A"`, `AUTOMATED_CELLS`, `MANUAL_CELLS`, `FIXTURE_IDS`; functions `validate_record(record: dict, expected_platform: str) -> None`, `serialize_record(record: dict) -> str`, `manual_template(platform_id: str) -> dict`, and CLI `--self-test`.
- Consumes later: Task 3 invokes the target and validates its record; Tasks 4–6 use the manual template and aggregate format.

- [ ] **Step 1: Add failing schema self-tests**

Inside `run_self_test()`, construct one valid synthetic record covering A01–A11 and assert that deleting each required host field, duplicating a cell, substituting `offscreen`, inserting `/Users/example/build`, adding a hostname, changing corpus revision, using an unknown fixture or omitting either `out_of_scope` entry raises `Refused`. Assert sorted compact JSON and exactly one trailing LF.

- [ ] **Step 2: Run the self-test and confirm RED**

Run:

```bash
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
```

Expected: non-zero because `validate_record`, `serialize_record` and `manual_template` are not implemented.

- [ ] **Step 3: Implement the minimal schema/refusal layer**

Use exact sets:

```python
AUTOMATED_CELLS = tuple(f"A{i:02d}" for i in range(1, 12))
MANUAL_CELLS = tuple(f"M{i:02d}" for i in range(1, 7))
FIXTURE_IDS = (
    "ui-rgb8-five-filters", "trace-dynamic-overlap-repeats",
    "ui-gray1-none", "ui-rgba16-byte-select", "error-truncated-token",
)
NATIVE_PLUGINS = {
    "windows-x64": {"windows"}, "macos-arm64": {"cocoa"},
    "ubuntu-lts-x64": {"xcb", "wayland"},
}
```

Validate SHA-256 with `^[0-9a-f]{64}$`, timestamps with UTC `Z`, exact cell sets, result vocabulary `PASS|BLOCKED|FAIL`, and recursive absolute-path/privacy-key rejection. `--self-test` writes nothing.

- [ ] **Step 4: Run self-test and syntax gate**

```bash
python3 -m py_compile scripts/run_wp_607a_native_gui_gate.py
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
```

Expected: both exit 0 and print `WP-607A self-test: PASS`.

- [ ] **Step 5: Commit**

```bash
git add scripts/run_wp_607a_native_gui_gate.py
git commit -m "test: freeze wp607a evidence contract"
```

### Task 2: Implement the native A01–A11 Qt gate

**Files:**
- Create: `tests/gui/wp_607a_native_gui_gate_test.cpp`
- Modify: `tests/gui/CMakeLists.txt`

**Interfaces:**
- Consumes: public `MainWindow::openFile`, stable object names, QAccessible, WP-607C registry via `PNGA_WP607C_CORPUS_DIR`, output root via `PNGA_WP607A_OUT`, platform id via `PNGA_WP607A_PLATFORM`.
- Produces: target `pnga_gui_wp_607a_native_gui_gate_tests`, CTest entry `gui_wp607a_native_gui_gate_tests`, and `<out>/automated.json` containing A01–A11.

- [ ] **Step 1: Add the self-asserting test skeleton and CMake registration**

Define QtTest slots named exactly:

```cpp
void openCloseReopen();
void dragDrop();
void menuShortcuts();
void dockFloatReset();
void keyboardFocus();
void accessibleTree();
void clipboard();
void rapidSwitch();
void chunkToFileBytes();
void stageToPixel();
void pixelToTokenBits();
void writeEvidence();
```

Register the target with the same GUI libraries/AUTOMOC set used by
`pnga_gui_wp_5u14n_native_capture_tests`, define `PNGA_WP607C_CORPUS_DIR`, and
label the CTest entry `gui;native;wp607a`. When the platform is offscreen or
minimal, every behavioral slot uses `QSKIP`; `writeEvidence` records no PASS.

- [ ] **Step 2: Build to confirm RED**

```bash
cmake --build --preset dev --target pnga_gui_wp_607a_native_gui_gate_tests --parallel 4
```

Expected: compile/link failure at the not-yet-defined fixture/evidence helpers.

- [ ] **Step 3: Implement fixture and record helpers**

Resolve each frozen id through the WP-607C `index.json`; reject missing id,
SHA mismatch and source-tree paths. Add `CellResult { QString id; QString
fixture_id; QString expected; QString result; QString note; }`. Populate host
facts from `QSysInfo`, `QLibraryInfo`, `QGuiApplication::platformName()`, the
primary screen and compile definitions. Do not record usernames or hostnames.

- [ ] **Step 4: Implement A01–A08**

Reuse the observable assertions already proven in
`main_window_layout_test.cpp`, `cross_platform_gui_gate_test.cpp` and
`compression_inspector_product_gate_test.cpp`, but execute them against a shown
real `MainWindow`. For A05 send actual Tab/Shift-Tab key events and assert the
focus sequence includes `xCoordinate`, `yCoordinate`, `lockCoordinate`,
`numericBase`, `previewTabs`, `hexSourceTabs` and `inspectorTabs`. For A08 alternate
the four lifecycle fixtures 12 times and assert the final title/context/rows
belong only to the twelfth generation.

- [ ] **Step 5: Implement A09–A11**

For A09 select a known Chunk and assert File Hex location/range. For A10 select
a delivered pixel and assert reconstruction Current context while manual
selection remains independent. For A11 use `trace-dynamic-overlap-repeats`,
lock pixel `(0,0)`, wait with a 10-second QtTest deadline for bounded Trace,
select one Match row and assert its typed DEFLATE range maps to every physical
span exposed to Hex. Timeouts are FAIL with the last visible status in `note`.

- [ ] **Step 6: Implement A06 native accessibility snapshot and evidence write**

Query `QAccessible::queryAccessibleInterface` for menus/actions, Chunk tree,
Preview tabs, Hex controls, coordinate controls, Inspector tabs/tables and
status labels. Assert expected role plus non-empty stable name and record state
or value. `writeEvidence` refuses missing/duplicate A ids and writes compact
sorted JSON using Qt JSON; Task 3 reserializes canonically after Python
validation.

- [ ] **Step 7: Verify offscreen isolation and full regression**

```bash
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'wp607a|main_window|cross_platform_gui|compression_inspector|trace_pipeline' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
```

Expected: existing suite 100% PASS; WP-607A native entry reports skipped and
does not create a PASS record.

- [ ] **Step 8: Commit**

```bash
git add tests/gui/wp_607a_native_gui_gate_test.cpp tests/gui/CMakeLists.txt
git commit -m "test: add wp607a native gui gate"
```

### Task 3: Complete the bounded native runner

**Files:**
- Modify: `scripts/run_wp_607a_native_gui_gate.py`

**Interfaces:**
- Produces: `python3 scripts/run_wp_607a_native_gui_gate.py --platform {windows-x64,macos-arm64,ubuntu-lts-x64} --preset dev --jobs 4`, `--manual-template <platform>`, and validated `<platform>/evidence.json`.
- Consumes: Task 2 target/JSON and WP-607C generation CTest.

- [ ] **Step 1: Extend the self-test with exact command plans**

Assert order: build target → bounded `wp607c_generate_corpus` → native platform
probe → bounded target invocation → schema validation → SHA assembly. Assert
host/platform mismatch, missing Windows interactive session, missing macOS
WindowServer, unset Linux `DISPLAY`+`WAYLAND_DISPLAY`, Xvfb marker and dirty
output directory all refuse before capture.

- [ ] **Step 2: Confirm RED, then implement orchestration**

```bash
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
```

Expected RED before implementation. Add 30-second platform probe, 300-second
gate timeout with streamed log, atomic temporary-output rename, appearance/
environment restoration and SIGTERM/CTRL_BREAK cleanup. Use direct target
execution so partial logs survive a hang.

- [ ] **Step 3: Implement manual template and aggregate validation**

`--manual-template` writes M01–M06 plus required scale rows with draft state
`UNREVIEWED`; this state is permitted only in an unsubmitted template. Aggregate
validation accepts only final results `PASS|BLOCKED|FAIL` and refuses final PASS
until every manual row is `PASS` and carries reviewer, UTC time and non-empty
semantic observation. The template excludes personal fields and screen-reader
audio.

- [ ] **Step 4: Verify runner contracts**

```bash
python3 -m py_compile scripts/run_wp_607a_native_gui_gate.py
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
QT_QPA_PLATFORM=offscreen python3 scripts/run_wp_607a_native_gui_gate.py --platform macos-arm64 --preset dev --jobs 4
```

Expected: first two PASS; the offscreen command refuses with non-zero status
and creates no PASS evidence.

- [ ] **Step 5: Commit**

```bash
git add scripts/run_wp_607a_native_gui_gate.py
git commit -m "test: add wp607a native evidence runner"
```

### Task 4: Execute and review the macOS automated matrix

**Files:**
- Create: `docs/evidence/wp-607a-native-gui-accessibility.md`

**Interfaces:**
- Consumes: Tasks 1–3 runner and local native macOS arm64 desktop.
- Produces: macOS A01–A11 dispositions and hashes in the tracked summary.

- [ ] **Step 1: Run the native macOS gate**

```bash
env -u QT_QPA_PLATFORM python3 scripts/run_wp_607a_native_gui_gate.py --platform macos-arm64 --preset dev --jobs 4
```

Expected: native plugin `cocoa`, A01–A11 PASS, validated ignored evidence and
an evidence-tree SHA. Any executed behavioral defect is FAIL, not BLOCKED.

- [ ] **Step 2: Review artifacts and write the summary header/macOS table**

Record exact command, commit, OS build, Qt, DPI/DPR, corpus revision,
`automated.json` SHA, aggregate SHA and every A-cell disposition. Add explicit
Statistics/APNG `out_of_scope` rows.

- [ ] **Step 3: Commit**

```bash
git add docs/evidence/wp-607a-native-gui-accessibility.md
git commit -m "docs: record wp607a macos automated evidence"
```

### Task 5: Add and execute Windows dispatch evidence

**Files:**
- Create: `.github/workflows/native-gui-accessibility.yml`
- Modify: `docs/evidence/wp-607a-native-gui-accessibility.md`

**Interfaces:**
- Consumes: pinned Qt/vcpkg/MSVC setup from `native-theme-capture.yml` and Task 3 runner.
- Produces: dispatch-only Windows artifact `wp607a-native-gui-accessibility-windows` and Windows A01–A11 summary rows.

- [ ] **Step 1: Add a dispatch-only bounded workflow**

Pin the existing action major versions, Python 3.11, `PyYAML==6.0.2`,
`aqtinstall==3.3.0` and Qt 6.8.3 `win64_msvc2022_64`. Configure/build the
corpus generator and WP-607A target, run the Windows runner with a 45-minute
job timeout, upload raw evidence plus logs under `if: always()`, and run no
push/PR trigger.

- [ ] **Step 2: Validate workflow structure locally**

```bash
python3 -c "import pathlib,yaml; d=yaml.load(pathlib.Path('.github/workflows/native-gui-accessibility.yml').read_text(), Loader=yaml.BaseLoader); assert set(d)=={'name','on','concurrency','jobs'}; assert list(d['jobs'])==['windows-gui-accessibility']"
git diff --check
```

Expected: exit 0.

- [ ] **Step 3: Commit and dispatch after the workflow exists on default branch**

```bash
git add .github/workflows/native-gui-accessibility.yml
git commit -m "ci: add wp607a windows native gui gate"
gh workflow run native-gui-accessibility.yml --ref main
```

If repository policy prevents publishing/dispatch, finish all local work and
record Windows automated cells BLOCKED with the exact unblock action. Do not
claim PASS without the downloaded artifact.

- [ ] **Step 4: Download, validate and record Windows evidence**

Run the runner's aggregate validator against the downloaded artifact, record
workflow run id/commit/machine facts and A01–A11/artifact hashes in the summary.
Any native rendering/session limitation that prevents execution is BLOCKED;
an executed product failure is FAIL.

- [ ] **Step 5: Commit**

```bash
git add docs/evidence/wp-607a-native-gui-accessibility.md
git commit -m "docs: record wp607a windows automated evidence"
```

### Task 6: Execute Ubuntu native automated evidence

**Files:**
- Modify: `docs/evidence/wp-607a-native-gui-accessibility.md`

**Interfaces:**
- Consumes: Ubuntu 24.04 LTS x86_64 real desktop with `xcb` or `wayland` and Task 3 runner.
- Produces: Ubuntu A01–A11 summary rows with display protocol/session evidence.

- [ ] **Step 1: Verify the session is native**

Record `XDG_SESSION_TYPE`, `DISPLAY`, `WAYLAND_DISPLAY`, Qt platform plugin,
logical DPI and DPR. Refuse offscreen/minimal/Xvfb-only/container-only
sessions. Do not convert such refusal into PASS.

- [ ] **Step 2: Run the native Ubuntu gate**

```bash
env -u QT_QPA_PLATFORM python3 scripts/run_wp_607a_native_gui_gate.py --platform ubuntu-lts-x64 --preset dev --jobs 4
```

Expected: `xcb` or `wayland`, A01–A11 PASS and validated evidence. If no
qualifying machine exists after local preparation, record BLOCKED with the
exact required environment.

- [ ] **Step 3: Record and commit**

```bash
git add docs/evidence/wp-607a-native-gui-accessibility.md
git commit -m "docs: record wp607a ubuntu automated evidence"
```

### Task 7: Execute scale and screen-reader manual units

**Files:**
- Modify: `docs/evidence/wp-607a-native-gui-accessibility.md`
- Generated only: `build/evidence/wp-607a/<platform-id>/manual.json`

**Interfaces:**
- Consumes: Task 3 manual templates, Windows Narrator, macOS VoiceOver, Ubuntu Orca and real desktop scale controls.
- Produces: M01–M06 and scale dispositions for all platforms.

- [ ] **Step 1: Generate one manual template per platform**

```bash
python3 scripts/run_wp_607a_native_gui_gate.py --manual-template windows-x64
python3 scripts/run_wp_607a_native_gui_gate.py --manual-template macos-arm64
python3 scripts/run_wp_607a_native_gui_gate.py --manual-template ubuntu-lts-x64
```

Expected: three ignored templates containing exact M01–M06/scale rows and no
personal fields.

- [ ] **Step 2: Product owner executes M01–M06 and scale rows**

Use Windows 100/150/200%, macOS native Retina plus one logical scaled case,
and Ubuntu 100/150/200% where exposed by the selected desktop. For M04 record
the announced name, role, state/value and changed selection/status semantic
tokens from Narrator/VoiceOver/Orca. Never infer screen-reader PASS from
QAccessible metadata alone.

- [ ] **Step 3: Validate manual records and update the tracked summary**

Run the aggregate validator; record reviewer/date, each result, observation
and manual JSON SHA. Unavailable access is BLOCKED; executed defects are FAIL.

- [ ] **Step 4: Commit**

```bash
git add docs/evidence/wp-607a-native-gui-accessibility.md
git commit -m "docs: record wp607a manual accessibility evidence"
```

### Task 8: Final verification and WP-607A closure

**Files:**
- Modify: `docs/development/wp-607a-native-gui-accessibility.md`
- Modify: `docs/development/wp-607-cross-platform-quality-evidence.md`
- Modify: `docs/evidence/wp-607a-native-gui-accessibility.md`

**Interfaces:**
- Consumes: every automated/manual record and artifact from Tasks 4–7.
- Produces: exactly one final WP-607A status and the parent status `WP-607A/C PASS` only when complete.

- [ ] **Step 1: Run final automated verification**

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --jobs 4
git diff --check
```

Expected: every command exits 0; native WP-607A test remains honestly skipped
under offscreen while the separate native records remain hash-valid.

- [ ] **Step 2: Audit coverage and hashes**

Require 33 automated results (11 × 3), 18 manual results (6 × 3), every scale
row, exact fixture/corpus revision, native plugin/session facts, two
`out_of_scope` declarations and zero unknown/missing/duplicate cells. Recompute
all artifact hashes from ignored evidence.

- [ ] **Step 3: Set exactly one status**

Set PASS only when every required row is PASS and every artifact validates.
Set BLOCKED only after all locally possible work is complete and a required
platform/hardware/screen-reader remains unavailable. Set FAIL for any executed
product defect or inconsistent evidence. Never leave `UNREVIEWED` in a final
record.

- [ ] **Step 4: Update package and parent, then commit**

On PASS, update the package to `PASS — package closed <date>` and parent to
`WP-607A/C PASS; WP-607B/D incomplete`. On BLOCKED/FAIL, preserve the parent as
WP-607C PASS and name the exact missing/failed cells.

```bash
git add docs/development/wp-607a-native-gui-accessibility.md docs/development/wp-607-cross-platform-quality-evidence.md docs/evidence/wp-607a-native-gui-accessibility.md
git commit -m "docs: close wp607a native gui accessibility evidence"
```

## Completion Definition

The frozen package applies verbatim. PASS requires all 33 automated platform
cells, all 18 manual platform cells, every required scale row, a complete
reviewer checklist, valid raw-artifact hashes, explicit Statistics/APNG
`out_of_scope` entries and zero unexplained native difference. WP-607B,
WP-607D and overall WP-607 remain incomplete after WP-607A closes.
