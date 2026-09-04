# WP-607A — Native GUI and Accessibility Evidence Summary

Tracked summary per `docs/development/wp-607a-native-gui-accessibility.md`
(Evidence contract). Raw records stay under the ignored
`build/evidence/wp-607a/<platform-id>/` trees; this file stores commands,
machine facts, per-cell dispositions and hashes of the ignored raw evidence.
Result vocabulary follows the package Error and status policy (R9): executed
defects are FAIL; platform/hardware/screen-reader access gaps are BLOCKED;
Statistics export and APNG timeline are explicit `out_of_scope`, never PASS.

**Status so far (Tasks 4–6):** macOS automated matrix executed and
re-executed after controller-authorized test fixes — final macOS record
**PASS (11/11)** (§2.3; original run: 8 PASS, 1 BLOCKED, 2 FAIL, §2.2).
Windows automated matrix executed and re-executed — final Windows record
**PASS (11/11)** (§3.2; original run: 9 PASS, 1 BLOCKED, 1 FAIL, §3.1).
Ubuntu cells BLOCKED (no Ubuntu 24.04 LTS desktop available to the product
owner). Final WP-607A status is set at Task 8, not here.

## 1. Shared facts

- Corpus revision:
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`
  (WP-607C frozen; fixture SHA-256 values carried inside every raw record).
- Automated cells: A01–A11 (schema `pnga-wp607a-native-gui-v1` v1, runner
  `scripts/run_wp_607a_native_gui_gate.py`).
- Statistics export and APNG timeline: **out_of_scope** (WP-602H / WP-706
  pending) — declared in every raw record and in section 6 below.

## 2. macOS arm64 — automated native matrix

- Command (runner-emitted plan verbatim):
  `python3 scripts/run_wp_607a_native_gui_gate.py --platform macos-arm64 --preset dev --jobs 4`
  Plan order: build target → bounded `wp607c_generate_corpus` (0.32 s) →
  30 s `-functions` native probe → one direct bounded capture (300 s kill,
  streamed logs) → schema validation → SHA assembly.
- Executed: 2026-09-04T16:48:43Z (record UTC timestamp).
- Host: macOS Tahoe 26.6.2 (Build 25G83), arm64, 8 GiB.
- Qt 6.11.1; `qt_platform_plugin` **cocoa** (native); display session `aqua`;
  logical DPI 72.00; device pixel ratio 2.00.
- Code state: compiled source = `79e4b95` (fix-round rebuild). The record's
  `git_commit` field `c0338735c1edc076a18aa9b3cc487e34327b0c89` is the
  configure-time bake (last reconfigure happened during Task 2, before
  `8db1528` existed); disclosed here rather than silently accepted.
- Raw evidence tree: `build/evidence/wp-607a/macos-arm64/`
  - `automated.json` SHA-256
    `96f7919bf55d01009e742733d68147c5a1a86453a791881ee80ae3ebe0a5a661`
  - Evidence-tree SHA-256
    `1ab6ea1724b3f82309dc42f2d94e9c413ab1a2faeb76a93865a6ee41e9446e94`
    (method: sorted relative paths, per-file SHA-256, sha256 over the
    `"<sha256>  <relpath>"` line list; files: `automated.json`,
    `logs/probe-functions.log`, `logs/qtest-wp607a-native-gui-gate.txt`,
    `logs/run-wp607a-native-gui-gate.log`).
  - `evidence.json` was intentionally **not** assembled: the runner refuses
    validation/assembly after a failed capture, so no aggregate exists for a
    FAIL run.

### 2.1 Session preflight disclosure (macOS 26)

macOS 26.6.2 launchd no longer exports `SECURITYSESSIONID` into the
environment of GUI processes (verified absent on Dock, Finder and Terminal
processes). The host is verifiably inside the logged-in Aqua session
(`launchctl managername` → `Aqua`, WindowServer running, console user
logged in). The runner's frozen preflight proxy was therefore satisfied by
restoring the **genuine** security session id obtained from
`SessionGetInfo(callerSecuritySession)` (attributes: graphic access, TTY,
non-remote) into the invoking environment — no runner or gate code was
modified, and nothing was fabricated. Nativeness of the record rests on the
`cocoa` plugin requirement in schema validation and the successful native
probe, not on the environment variable. Flagged for controller review: the
preflight heuristic needs a macOS-26-compatible session check in a
subsequently authorized amendment.

### 2.2 macOS cell dispositions

| Cell | Disposition | Note |
|---|---|---|
| A01 open-close-reopen | PASS | Open, close and reopen of `ui-rgb8-five-filters.png` kept title, image, chunk tree and Close action coherent. |
| A02 drag-drop | BLOCKED | **Superseded by the §2.3 re-execution: PASS.** Original record kept below. Raw record FAIL is the cell's own setup abort: the second `QFile::copy` (to `wp607a-drop.png`) failed at `tests/gui/wp_607a_native_gui_gate_test.cpp:793` because case-insensitive APFS treats it as the same name as the earlier `wp607a-drop.PNG` copy (deterministically reproduced standalone). No product drag/drop interaction executed. The platform filesystem property prevents the harness (both extension variants in one temp directory); unblock = controller-authorized test-only amendment (per-extension subdirectories) or a case-sensitive volume. Not a product defect; recorded BLOCKED per R9, disclosed against the raw FAIL. |
| A03 menu-shortcuts | PASS | File/View identities, native Open/Close/Quit shortcuts and visibility toggles retained their frozen identities. |
| A04 dock-float-reset | PASS | Both docks floated and Reset Layout restored areas, visibility and bounded widths. |
| A05 keyboard-focus | FAIL | **Superseded by the §2.3 re-execution: PASS.** Original record kept below. Executed Tab walk never reached `lockCoordinate`. Host default keyboard navigation: `AppleKeyboardUIMode` unset = macOS Full Keyboard Access off; Qt's macOS style then excludes buttons/checkboxes from Tab traversal, so the product-declared order (x → y → lock → base → preview → hex → inspector, `main_window_ui.cpp:287`) breaks at the lock checkbox. Executed observation not met → FAIL per R9. A product fix (explicit Tab-reachability) requires a separately authorized defect package; the host OS keyboard setting was deliberately NOT flipped to force a PASS. The M02 manual keyboard checklist remains the real-user authority. |
| A06 accessible-tree | FAIL | **Superseded by the §2.3 re-execution: PASS.** Original record kept below. Executed snapshot: six named controls report `invisible` — `compressionInspectorPages`, `blockInspector`, `compressionBlocksTable`, `compressionHuffmanTable`, `compressionDecodeTraceTable`, `compressionContextStatus`. All live inside non-current tab pages (`inspectorTabs` "Compression" tab; `compressionInspectorPages` Blocks/Huffman/Decode Trace pages), and QAccessible reporting hidden pages invisible is correct accessibility semantics (hidden controls must not be announced). No role/name/state violations were recorded for any control. The cell's visibility requirement therefore over-asserts at default state — a test-design defect, not a product defect. Unblock = controller-authorized amendment asserting visible state only for currently displayed controls and role/name/state for hidden ones. |
| A07 clipboard | PASS | Synthetic value round-trip per product-gate precedent (5U12F) through the native clipboard; analysis generation and ready state stayed unchanged. |
| A08 rapid-switch | PASS | 12 alternating valid/malformed opens published only the twelfth generation with ready context, rows and image; close and reopen stayed responsive and restored a usable document. |
| A09 chunk-file-bytes | PASS | Selecting the IHDR and IDAT rows navigated File Hex to the exact chunk header offsets 8 and 33 with envelope highlights. |
| A10 stage-pixel | PASS | Selecting delivered pixel (1, 1) updated the reconstruction report and Compression Current context; a manual Decode Trace row selection stayed independent. |
| A11 pixel-token-bits | PASS | Match token 2 carried DeflateBitRange [128, 132) mapped to 1 physical File span in Hex (bounded Trace resolved under the 10 s deadline). |
| statistics-export | out_of_scope | Statistics export remains out_of_scope until WP-602H passes; never PASS. |
| apng-timeline | out_of_scope | APNG timeline behavior remains out_of_scope until WP-706 passes; never PASS. |

macOS automated status of the original run: **FAIL** (A05/A06 executed
failures recorded honestly; A02 recorded BLOCKED with the exact unblock
action). Superseded by the §2.3 re-execution after controller-authorized
test-only fixes; per the package policy, production fixes require separate
defect packages with failing tests, and this evidence run changes no
production code.

### 2.3 macOS re-execution (controller-authorized test fixes)

Re-executed after the fix commit `fc7aef2` ("repair a02 staging, a05 fka
and a06 per-page assertions") with the same command and output root, per
the controller rulings:

1. A02 staging uses distinct temp filenames (`wp607a-drop-upper.PNG`,
   `wp607a-drop-lower.png`), so no copy ever lands on a case-variant of an
   existing name; the non-PNG rejection case stays testable.
2. The runner's macOS plan now wraps the capture in an enable-fka /
   restore-fka pair: save `AppleKeyboardUIMode` → `defaults write
   NSGlobalDomain AppleKeyboardUIMode -int 3` → read-back verified → run
   the gate → restore (delete when the key was absent) → read-back
   verified; any write/restore mismatch is REFUSED, and the restore also
   runs on every failure path after the enable (try/finally). This host
   was observed with the key ABSENT before and after the run (verified).
3. A06 activates each relevant tab page before asserting that page's
   controls (Compression container → DEFLATE Blocks → Huffman → Decode
   Trace) and compares role KINDs (semantic names, e.g. Pane/Window as one
   container kind), never raw platform role ids.
4. Preflight: macOS 26 no longer exports `SECURITYSESSIONID`; the same
   OS-verified session restoration as §2.1 applied (no behavior change).

- Command: `env -u QT_QPA_PLATFORM SECURITYSESSIONID=<genuine session id> python3 scripts/run_wp_607a_native_gui_gate.py --platform macos-arm64 --preset dev --jobs 4`
  (runner-emitted plan: build → corpus fixture → probe → enable-fka →
  capture → restore-fka → validate → assemble).
- Result: **PASS — 11/11 cells**; gate exit 0; validated record and
  aggregate `evidence.json` assembled.
- A02 PASS: "local .PNG and .png drops opened the dropped document and the
  .txt drop was rejected" (product drag/drop behavior now genuinely
  executed on case-insensitive APFS).
- A05 PASS: "real Tab/Shift-Tab events covered xCoordinate, yCoordinate,
  lockCoordinate, numericBase, previewTabs, hexSourceTabs and
  inspectorTabs; traversal wrapped without a trap" (with FKA enabled by
  the runner, restored afterwards).
- A06 PASS: "QAccessible snapshot, 29 entries carrying actual
  role/name/state/value tokens …" with every control asserted visible in
  its activated page; chunk-tree fallback + M04 escalation unchanged.
- Host/Qt facts unchanged from §2 (macOS Tahoe 26.6.2 arm64, Qt 6.11.1,
  cocoa, aqua, 72.00 DPI, DPR 2.00); record baked `git_commit` remains
  `c0338735…` (configure-time; compiled source = `fc7aef2`).
- Raw evidence tree: `build/evidence/wp-607a/macos-arm64/`
  - `automated.json` SHA-256
    `8ef8cee49bc9d43fa849bdfef4a70f10aa023796aaafe22c62f54c8f53bfe76c`
  - Aggregate `evidence.json` SHA-256
    `abb49ae44ed29415b3b86b0a3ac541f8fe811a76d57d650eb0d145e2f704a768`
  - Evidence-tree SHA-256 (sorted `sha256  relpath` lines over
    `automated.json`, `evidence.json` and the three log files; the ignored
    manual template is excluded)
    `155683c8ed784c9274d26103fa1a06e264071cd4b317a5f37a24654c558d00a8`

## 3. Windows x64 — automated native matrix (CI dispatch)

Dispatch-only workflow `.github/workflows/native-gui-accessibility.yml`
(job `windows-gui-accessibility`, `windows-latest`, 45-minute timeout):
pinned actions, Python 3.11, `PyYAML==6.0.2`, MSVC DevShell, vcpkg tool +
binary caches with `VCPKG_BINARY_SOURCES`, aqtinstall 3.3.0 + Qt 6.8.3
`win64_msvc2022_64`, byte-identical (LF) checkout with a frozen corpus
revision guard, builds the corpus generator and the WP-607A target, runs the
bounded Windows runner (Qt runtime PATH derived by the runner, L1), uploads
`build/evidence/wp-607a/**` and the ctest log under `if: always()`.
No push/PR triggers.

- Executing run: [33900627092](https://github.com/babybandf/PNG-Analyzer/actions/runs/33900627092)
  (dispatch `--ref main` at `763067277b40c4a7ea6e838693732aa5993a303f`,
  the exact commit the CI configure baked into the record).
- Disclosed pre-execution dispatches (both pre-capture runner refusals,
  no evidence produced): run 33898311021 — the hosted agent environment
  lacks `SESSIONNAME`; run 33899043492 — Windows CRLF checkout changed the
  hashed corpus input bytes (revision `33e10284…` ≠ frozen `5df99a…`).
  Two environment restorations were added to the workflow, both OS-observed
  and disclosed: `SESSIONNAME=Console` is exported only when the OS confirms
  the agent shares its session with the interactive desktop shell
  (explorer.exe) — the runner's frozen preflight still performs its check —
  and `core.autocrlf=false` is pinned before checkout plus a fast-failing
  frozen-revision guard, making the checkout byte-identical.
- Record facts (from the validated artifact): `qt_platform_plugin`
  **windows** (native); display session `win32-desktop` (hosted interactive
  desktop); Windows Server 2025 Version 24H2, x86_64, 15 GiB
  (`machine_label` is the fixed non-sensitive constant
  `wp607a-local-desktop`); Qt 6.8.3; logical DPI 96.00; DPR 1.00.
- Raw evidence tree (artifact `wp607a-native-gui-accessibility-windows`,
  paths under `evidence/wp-607a/windows-x64/`):
  - `automated.json` SHA-256
    `5fbea93cd5e43530b768d0757b65226068ad83db89595b906412468970f22c5a`
  - Evidence-tree SHA-256
    `2e2f0b87a8d0be4e0a1d1e1fc585c419f296242d0d276a5ece8ca7c547ebc9cc`
    (same method as section 2; files: `automated.json`,
    `logs/probe-functions.log`, `logs/qtest-wp607a-native-gui-gate.txt`,
    `logs/run-wp607a-native-gui-gate.log`).
  - `automated.json` passed the runner's frozen schema validation
    (`validate_record(record, "windows-x64")` — run locally against the
    downloaded artifact). `evidence.json` was not assembled by CI because
    the runner refuses assembly after a failed capture (2 failed cells).

### 3.1 Windows cell dispositions

| Cell | Disposition | Note |
|---|---|---|
| A01 open-close-reopen | PASS | Open, close and reopen of `ui-rgb8-five-filters.png` kept title, image, chunk tree and Close action coherent. |
| A02 drag-drop | BLOCKED | **Superseded by the §3.2 re-execution: PASS.** Original record kept below. Raw record FAIL is the same cell-setup abort as macOS: the second `QFile::copy` failed at `tests/gui/wp_607a_native_gui_gate_test.cpp:793` — case-insensitive NTFS treats `wp607a-drop.png` as the already-created `wp607a-drop.PNG`. No product drag/drop interaction executed; same unblock action as section 2.2 (A02). |
| A03 menu-shortcuts | PASS | File/View identities, native Open/Close/Quit shortcuts and visibility toggles retained their frozen identities. |
| A04 dock-float-reset | PASS | Both docks floated and Reset Layout restored areas, visibility and bounded widths. Qt logged a non-fatal hosted-VM geometry clamp (`QWindowsWindow::setGeometry` on `HyperVMonitor`: requested 1200x760, resulting 1028x749); the cell's assertions all passed at the clamped size. |
| A05 keyboard-focus | PASS | Real Tab/Shift-Tab events covered xCoordinate, yCoordinate, lockCoordinate, numericBase, previewTabs, hexSourceTabs and inspectorTabs; traversal wrapped without a trap. (Windows does not gate button/checkbox Tab-reachability behind Full Keyboard Access, unlike macOS — cf. section 2.2 A05.) |
| A06 accessible-tree | FAIL | **Superseded by the §3.2 re-execution: PASS.** Original record kept below. Executed snapshot: `chunksDock:role-9` plus the same six inactive-tab-page invisibles as macOS (`compressionInspectorPages`, `blockInspector`, the three compression tables, `compressionContextStatus`). Role 9 = `Window`: the Windows QAccessible backend maps the `QDockWidget` to `Window`, while the frozen expectation `Pane` was taken from the macOS mapping — platform backend behavior, not a product defect; the dock exposes a non-empty stable name and usable state on both. The six invisibles are the same test over-assertion (correct a11y semantics for hidden pages). Unblock = controller-authorized amendment (per-platform role expectation; visible-state assertions only for currently displayed controls). |
| A07 clipboard | PASS | Synthetic value round-trip per product-gate precedent (5U12F) through the native clipboard; analysis generation and ready state stayed unchanged. |
| A08 rapid-switch | PASS | 12 alternating valid/malformed opens published only the twelfth generation with ready context, rows and image; close and reopen stayed responsive and restored a usable document. |
| A09 chunk-file-bytes | PASS | Selecting the IHDR and IDAT rows navigated File Hex to the exact chunk header offsets 8 and 33 with envelope highlights. |
| A10 stage-pixel | PASS | Selecting delivered pixel (1, 1) updated the reconstruction report and Compression Current context; a manual Decode Trace row selection stayed independent. |
| A11 pixel-token-bits | PASS | Match token 2 carried DeflateBitRange [128, 132) mapped to 1 physical File span in Hex (bounded Trace resolved under the 10 s deadline). |
| statistics-export | out_of_scope | Declared in the record; never PASS. |
| apng-timeline | out_of_scope | Declared in the record; never PASS. |

Windows automated status of the original run: **FAIL** (A06 executed
failure recorded honestly; A02 recorded BLOCKED with the exact unblock
action, disclosed against the raw FAIL). Superseded by the §3.2
re-execution after controller-authorized test-only fixes. No production
code changed.

### 3.2 Windows re-execution (controller-authorized test fixes)

Re-dispatched after the fix commit `fc7aef2` reached main (same workflow —
no workflow changes needed for this fix round; the Windows plan carries no
FKA steps):

- Executing run: [33902790935](https://github.com/babybandf/PNG-Analyzer/actions/runs/33902790935)
  (dispatch `--ref main` at `fc7aef2e5bbb8ae96afde9c30ba778d111042cad` —
  the exact commit the CI configure baked into the record). Gate step
  green; run fully green.
- Record facts: `qt_platform_plugin` **windows**, session
  `win32-desktop`, Windows Server 2025 Version 24H2, x86_64, Qt 6.8.3,
  DPI 96.00, DPR 1.00 (unchanged from §3).
- Result: **PASS — 11/11 cells**; validated record and aggregate
  `evidence.json` assembled on CI; `validate_record` passed locally
  against the downloaded artifact.
  - A02 PASS: "local .PNG and .png drops opened the dropped document and
    the .txt drop was rejected" (distinct staging names; product behavior
    genuinely executed on case-insensitive NTFS).
  - A06 PASS: 29 snapshot entries with role-kind comparisons
    (Pane/Window as one container kind) and per-page activation of the
    Compression container and its three pages; chunk-tree fallback + M04
    escalation unchanged.
  - A05 PASS again (Windows unchanged by the FKA ruling).
- Raw evidence tree (artifact `wp607a-native-gui-accessibility-windows`,
  paths under `evidence/wp-607a/windows-x64/`):
  - `automated.json` SHA-256
    `a86f34169b2f1d6d6462680d4ddbe4fd395d04fe11ef4bdcfb4e121aa200c268`
  - Aggregate `evidence.json` SHA-256
    `f467d5ade09a314b1d5c9fdc7d059eeb5939728ca845277f7936fc02e28c3f1b`
  - Evidence-tree SHA-256 (same method as §2.3, over `automated.json`,
    `evidence.json` and the three log files)
    `6094ca3149d76b18b1c3163910c23eb041ca51806ecff829fa495475f52719eb`

Windows re-executed status: **PASS**.

## 4. Ubuntu 24.04 LTS x86_64 — automated native matrix

**All A01–A11 cells: BLOCKED.**

Reason: no Ubuntu 24.04 LTS desktop exists that is available to the product
owner, so the native `xcb`/`wayland` run cannot be executed or faked
(product-owner decision, 2026-09-04; package Error and status policy).

| Cell | Disposition | Note |
|---|---|---|
| A01 open-close-reopen | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A02 drag-drop | BLOCKED | Same reason; never executed (the macOS/Windows harness note in sections 2.2/3.1 does not apply here — the cell was not run at all). |
| A03 menu-shortcuts | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A04 dock-float-reset | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A05 keyboard-focus | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A06 accessible-tree | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A07 clipboard | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A08 rapid-switch | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A09 chunk-file-bytes | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A10 stage-pixel | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| A11 pixel-token-bits | BLOCKED | No qualifying Ubuntu 24.04 LTS desktop available to the product owner; never executed. |
| statistics-export | out_of_scope | Declared for the package; never PASS. |
| apng-timeline | out_of_scope | Declared for the package; never PASS. |

No automated record, fixture SHA or artifact hash exists for Ubuntu: no raw
evidence was produced, and none may be invented.

Required environment to unblock:

- Ubuntu 24.04 LTS x86_64 physical or bare-metal-class machine;
- a real desktop session (`XDG_SESSION_TYPE` = `x11` (xcb) or `wayland`),
  recorded explicitly with `DISPLAY`/`WAYLAND_DISPLAY` facts;
- a scale-capable desktop exposing the 100%/150%/200% scale matrix;
- Orca installed for the M04 screen-reader manual unit;
- the standard gate command then applies:
  `env -u QT_QPA_PLATFORM python3 scripts/run_wp_607a_native_gui_gate.py --platform ubuntu-lts-x64 --preset dev --jobs 4`.

Xvfb/offscreen/minimal or container-only sessions cannot satisfy any cell
(R3) and are refused by the runner.

## 5. Manual matrices (M01–M06) and scale rows

Manual templates are generated per platform by
`python3 scripts/run_wp_607a_native_gui_gate.py --manual-template <platform>`
into the ignored evidence trees. Product-owner execution, aggregate
validation and dispositions are recorded here at Task 7.

## 6. Out-of-scope declarations (explicit, per package)

| Workflow | Declaration |
|---|---|
| Statistics export | `out_of_scope` — WP-602H pending; never PASS in any WP-607A record. |
| APNG timeline | `out_of_scope` — WP-706 pending; never PASS in any WP-607A record. |
