# WP-607A — Native GUI and Accessibility Evidence Summary

Tracked summary per `docs/development/wp-607a-native-gui-accessibility.md`
(Evidence contract). Raw records stay under the ignored
`build/evidence/wp-607a/<platform-id>/` trees; this file stores commands,
machine facts, per-cell dispositions and hashes of the ignored raw evidence.
Result vocabulary follows the package Error and status policy (R9): executed
defects are FAIL; platform/hardware/screen-reader access gaps are BLOCKED;
Statistics export and APNG timeline are explicit `out_of_scope`, never PASS.

**Status so far (Tasks 4–6):** macOS automated matrix executed — 8 PASS,
1 BLOCKED (A02 harness/filesystem), 2 FAIL (A05, A06 — see notes). Windows
automated matrix dispatched via CI (section 3). Ubuntu cells BLOCKED (no
Ubuntu 24.04 LTS desktop available to the product owner). Final WP-607A
status is set at Task 8, not here.

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
| A02 drag-drop | BLOCKED | Raw record FAIL is the cell's own setup abort: the second `QFile::copy` (to `wp607a-drop.png`) failed at `tests/gui/wp_607a_native_gui_gate_test.cpp:793` because case-insensitive APFS treats it as the same name as the earlier `wp607a-drop.PNG` copy (deterministically reproduced standalone). No product drag/drop interaction executed. The platform filesystem property prevents the harness (both extension variants in one temp directory); unblock = controller-authorized test-only amendment (per-extension subdirectories) or a case-sensitive volume. Not a product defect; recorded BLOCKED per R9, disclosed against the raw FAIL. |
| A03 menu-shortcuts | PASS | File/View identities, native Open/Close/Quit shortcuts and visibility toggles retained their frozen identities. |
| A04 dock-float-reset | PASS | Both docks floated and Reset Layout restored areas, visibility and bounded widths. |
| A05 keyboard-focus | FAIL | Executed Tab walk never reached `lockCoordinate`. Host default keyboard navigation: `AppleKeyboardUIMode` unset = macOS Full Keyboard Access off; Qt's macOS style then excludes buttons/checkboxes from Tab traversal, so the product-declared order (x → y → lock → base → preview → hex → inspector, `main_window_ui.cpp:287`) breaks at the lock checkbox. Executed observation not met → FAIL per R9. A product fix (explicit Tab-reachability) requires a separately authorized defect package; the host OS keyboard setting was deliberately NOT flipped to force a PASS. The M02 manual keyboard checklist remains the real-user authority. |
| A06 accessible-tree | FAIL | Executed snapshot: six named controls report `invisible` — `compressionInspectorPages`, `blockInspector`, `compressionBlocksTable`, `compressionHuffmanTable`, `compressionDecodeTraceTable`, `compressionContextStatus`. All live inside non-current tab pages (`inspectorTabs` "Compression" tab; `compressionInspectorPages` Blocks/Huffman/Decode Trace pages), and QAccessible reporting hidden pages invisible is correct accessibility semantics (hidden controls must not be announced). No role/name/state violations were recorded for any control. The cell's visibility requirement therefore over-asserts at default state — a test-design defect, not a product defect. Unblock = controller-authorized amendment asserting visible state only for currently displayed controls and role/name/state for hidden ones. |
| A07 clipboard | PASS | Synthetic value round-trip per product-gate precedent (5U12F) through the native clipboard; analysis generation and ready state stayed unchanged. |
| A08 rapid-switch | PASS | 12 alternating valid/malformed opens published only the twelfth generation with ready context, rows and image; close and reopen stayed responsive and restored a usable document. |
| A09 chunk-file-bytes | PASS | Selecting the IHDR and IDAT rows navigated File Hex to the exact chunk header offsets 8 and 33 with envelope highlights. |
| A10 stage-pixel | PASS | Selecting delivered pixel (1, 1) updated the reconstruction report and Compression Current context; a manual Decode Trace row selection stayed independent. |
| A11 pixel-token-bits | PASS | Match token 2 carried DeflateBitRange [128, 132) mapped to 1 physical File span in Hex (bounded Trace resolved under the 10 s deadline). |
| statistics-export | out_of_scope | Statistics export remains out_of_scope until WP-602H passes; never PASS. |
| apng-timeline | out_of_scope | APNG timeline behavior remains out_of_scope until WP-706 passes; never PASS. |

macOS automated status: **FAIL** (A05/A06 executed failures recorded
honestly; A02 recorded BLOCKED with the exact unblock action). Per the
package policy, production fixes require separate defect packages with
failing tests; this evidence run changes no production code.

## 3. Windows x64 — automated native matrix (CI dispatch)

Dispatch-only workflow `.github/workflows/native-gui-accessibility.yml`
(job `windows-gui-accessibility`, `windows-latest`, 45-minute timeout):
pinned actions, Python 3.11, `PyYAML==6.0.2`, MSVC DevShell, vcpkg tool +
binary caches with `VCPKG_BINARY_SOURCES`, aqtinstall 3.3.0 + Qt 6.8.3
`win64_msvc2022_64`, builds the corpus generator and the WP-607A target,
runs the bounded Windows runner (Qt runtime PATH derived by the runner, L1),
uploads `build/dev/evidence/wp-607a/**` and logs under `if: always()`.
No push/PR triggers.

- Run: see section 3.1 (recorded after artifact validation).

## 4. Ubuntu 24.04 LTS x86_64 — automated native matrix

**All A01–A11 cells: BLOCKED.**

Reason: no Ubuntu 24.04 LTS desktop exists that is available to the product
owner, so the native `xcb`/`wayland` run cannot be executed or faked
(product-owner decision, 2026-09-04; package Error and status policy).

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
