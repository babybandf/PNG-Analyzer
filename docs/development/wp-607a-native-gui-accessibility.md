# WP-607A — Native GUI and Accessibility Evidence

Status: **approved; frozen for implementation** (2026-09-04)

Parent package: `wp-607-cross-platform-quality-evidence.md`.
Written-package review: `wp-607a-written-package-review.md` (rulings R1–R10
bind the implementation plan).

## Goal

Produce auditable native-window evidence that the completed static PNG product
is operable on Windows x64, macOS arm64 and Ubuntu LTS x86_64 through mouse,
keyboard and each platform's screen reader. The package verifies existing
behavior; it does not redesign the application or add product capability.

## Dependencies

- WP-5U15: PASS.
- WP-5U12F: PASS.
- WP-5U14N: PASS.
- WP-607C controlled corpus: PASS, revision
  `5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f`.

## Scope and non-goals

WP-607A owns native execution infrastructure, automated behavioral records,
manual interaction/screen-reader checklists and the final evidence summary for
the static-v1 GUI.

It does not own:

- Statistics export, which remains `out_of_scope` until WP-602H passes;
- APNG timeline behavior, which remains `out_of_scope` until WP-706 passes;
- theme redesign, performance thresholds or the WP-607D aggregate audit;
- production fixes discovered during evidence collection.

A reproducible product defect is recorded as `FAIL`. Its fix requires a
separate focused failing test and separately authorized change; WP-607A does
not silently widen into production work.

## Platform topology

| Platform id | Required environment | Native platform plugin | Screen reader |
|---|---|---|---|
| `windows-x64` | supported stable Windows x64 desktop; CI may cover base-scale automated cells | `windows` | Narrator |
| `macos-arm64` | supported stable macOS arm64 desktop | `cocoa` | VoiceOver |
| `ubuntu-lts-x64` | Ubuntu 24.04 LTS x86_64 desktop, X11 or Wayland recorded explicitly | `xcb` or `wayland` | Orca |

`offscreen`, `minimal`, Xvfb-only, RDP sessions that suppress native desktop
interaction, and container-only runs may exercise self-tests but cannot satisfy
a native matrix cell. The evidence records the exact OS build actually used;
the table freezes the representative platform class, not an invented support
claim for untested releases.

## Controlled fixtures

All product workflows use WP-607C generated fixtures and record fixture SHA-256:

| Fixture id | Use |
|---|---|
| `ui-rgb8-five-filters` | File Open, drag/drop, Chunk→File bytes, Stage→Pixel, menus, docks and clipboard |
| `trace-dynamic-overlap-repeats` | Pixel→Token→compressed bits and bounded Decode Trace |
| `ui-gray1-none` | rapid-switch alternate document and narrow-window checks |
| `ui-rgba16-byte-select` | rapid-switch byte-selection boundary |
| `error-truncated-token` | malformed-document replacement and preserved diagnostic state |

No external fixture or unrecorded local image may satisfy a required cell.

## Evidence contract

Generated records use schema `pnga-wp607a-native-gui-v1` and live below
`build/evidence/wp-607a/<platform-id>/` (never committed). Every platform
record contains:

- `schema_version=1`, `work_package="WP-607A"`, final status and UTC time;
- git commit, command, OS/build, architecture, compiler and Qt version;
- native Qt platform plugin, display protocol/session type, logical DPI and
  device-pixel ratio;
- CPU, memory, non-sensitive machine label and WP-607C corpus revision;
- per-cell id, automated/manual form, fixture id/SHA-256, expected observation,
  actual result and artifact SHA-256;
- explicit `out_of_scope` entries for Statistics export and APNG timeline.

Records must contain no absolute source/build path, username, hostname,
clipboard contents, screen-reader speech capture or other personal data.
Generated JSON uses sorted keys, ASCII escaping and one trailing LF. Missing
fields, unknown cells, duplicate cells, hash mismatch or an unrecognized
platform makes validation fail.

The tracked summary is `docs/evidence/wp-607a-native-gui-accessibility.md`.
It stores commands, machine facts, matrix dispositions and hashes of ignored
raw evidence; it does not copy binary captures into git.

## Automated matrix

The native test target runs the same cells on all three platform classes at
their base/native scale. Each cell must pass on each platform; an unavailable
platform is `BLOCKED`, not `NOT_CONFIGURED` PASS.

| Cell | Required observation |
|---|---|
| `A01-open-close-reopen` | Open fixture, visible title/state, close clears document, reopen restores a usable document |
| `A02-drag-drop` | `.png`/`.PNG` local URL accepted; non-PNG rejected; opened document matches dropped fixture |
| `A03-menu-shortcuts` | File/View actions, native Open/Close/Quit shortcuts and visibility toggles retain frozen identities |
| `A04-dock-float-reset` | both docks move/float and Reset Layout restores areas, visibility and bounded widths |
| `A05-keyboard-focus` | keyboard-only focus reaches coordinate, Preview, Hex and Inspector controls without a trap |
| `A06-accessible-tree` | required controls expose non-empty stable names and expected roles/states through `QAccessible` |
| `A07-clipboard` | selectable/copyable value round-trips through the native clipboard without altering analysis state |
| `A08-rapid-switch` | 12 alternating valid/malformed opens publish only the final generation; close/reopen remains responsive |
| `A09-chunk-file-bytes` | selecting a Chunk navigates File Hex to its exact source range |
| `A10-stage-pixel` | selecting a reconstruction stage/pixel updates Current context without corrupting manual selection |
| `A11-pixel-token-bits` | locked pixel resolves to bounded Trace and typed compressed physical spans across the pipeline |

The native target may reuse public `MainWindow::openFile`, existing object
names and the WP-607C build-tree registry. It must not add production test
hooks. Automated checks supplement but never replace the manual screen-reader
and real pointer/drag observations below.

## Native scale matrix

| Platform | Required scales |
|---|---|
| Windows | 100%, 150%, 200% desktop scale |
| macOS | native Retina plus one logical scaled case |
| Ubuntu | 100%, 150%, 200% desktop scale where the selected X11/Wayland desktop exposes them |

At each required scale, record logical DPI, DPR, window size and whether text,
focus rings, menus, docks and the three core workflows remain usable. A scale
not supported by the selected Ubuntu desktop carries availability
`not_supported` and result `BLOCKED` with desktop evidence; it does not silently
become PASS. The reviewer decides whether another supported desktop run is
needed before closure.

## Manual matrix

The product owner executes M01–M06 on each platform. A single checklist row may
reference multiple screenshots/notes, but every platform/cell pair has its own
result.

| Cell | Manual observation |
|---|---|
| `M01-open-dragdrop` | Native File Open and pointer drag/drop both work; rejection feedback is understandable |
| `M02-keyboard-focus` | keyboard-only core workflows complete; focus order and visible focus are logical; no trap |
| `M03-docks-scale` | pointer dock drag/float/redock/reset works at every required scale without clipping |
| `M04-screen-reader` | Narrator/VoiceOver/Orca announces control name, role, state/value and meaningful selection/status changes |
| `M05-clipboard` | native copy/paste works for a representative value without unexpected formatting or state changes |
| `M06-lifecycle` | close, reopen and rapid switching show no stale image, Chunk, stage, selection, trace or announcement |

Screen-reader wording is platform-owned and need not be byte-identical. The
required semantic tokens are the product control name, role, current state or
value, and the changed selection/status. The checklist records observed tokens
and disposition; audio recording is neither required nor accepted as the sole
evidence.

## Error and status policy

- `PASS`: every required automated and manual platform/cell pair passes, every
  required scale is resolved, all records validate and no unexplained
  difference remains.
- `BLOCKED`: all locally possible work is complete but platform, desktop,
  hardware or screen-reader access prevents one or more required cells.
- `FAIL`: a required cell executes and exposes a product defect, inconsistent
  evidence, missing/stale publication, invalid record or artifact hash failure.
- `NOT_CONFIGURED` is a runner observation only and never closes a cell.

## Allowed paths

- `tests/gui/wp_607a_native_gui_gate_test.cpp`
- focused registration in `tests/gui/CMakeLists.txt`
- `scripts/run_wp_607a_native_gui_gate.py`
- `.github/workflows/native-gui-accessibility.yml`
- `docs/development/wp-607a-*.md`, the WP-607 parent status and
  `docs/evidence/wp-607a-native-gui-accessibility.md`
- ignored evidence under `build/evidence/wp-607a/**`

## Forbidden paths

- `libs/**`, `ui/qt/**`, `apps/**`, `third_party/**`, packaging and corpus
  generator/manifest changes;
- new dependency, package manager, top-level directory or production test hook;
- offscreen/Xvfb evidence labeled native;
- Statistics/APNG/Compare surface or inference about untested operating systems;
- weakening, skipping or deleting an existing test to make a cell pass.

## Verification

```text
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
python3 scripts/run_wp_607a_native_gui_gate.py --self-test
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py --preset asan --jobs 4
git diff --check
```

Native platform commands are emitted by the runner and recorded verbatim in
the evidence summary.

## Completion definition

WP-607A closes only when all A01–A11 and M01–M06 platform pairs, scale rows,
record hashes and the reviewer checklist are resolved according to the policy
above. Completion updates the WP-607 parent to `WP-607A/C PASS`; WP-607B,
WP-607D and overall WP-607 remain incomplete.
