#!/usr/bin/env python3
"""WP-607A native GUI and accessibility evidence runner.

Builds the test-only Qt gate target, materializes the WP-607C corpus with one
explicit bounded ctest fixture step, runs a 30s Qt platform probe, then drives
the real MainWindow through one DIRECT bounded native invocation of
pnga_gui_wp_607a_native_gui_gate_tests (never offscreen) under a 300s kill
timeout with streamed logs, validates the emitted pnga-wp607a-native-gui-v1
record and assembles build/evidence/wp-607a/<platform-id>/evidence.json
(sorted keys, ASCII escaping, one trailing LF, UTC timestamps).

The runner refuses: host/platform mismatch, offscreen/minimal capture
attempts, missing Windows interactive sessions, missing macOS WindowServer
access, Linux hosts without DISPLAY/WAYLAND_DISPLAY, Xvfb-only sessions,
dirty output directories, missing corpus fixtures and any record violating
the frozen schema.

Usage:
    python3 scripts/run_wp_607a_native_gui_gate.py \
        --platform macos-arm64 --preset dev --jobs 4
    python3 scripts/run_wp_607a_native_gui_gate.py --self-test
"""

import argparse
import datetime
import hashlib
import json
import os
import platform as host_platform
import re
import signal
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCHEMA = "pnga-wp607a-native-gui-v1"
SCHEMA_VERSION = 1
WORK_PACKAGE = "WP-607A"
GATE_TARGET = "pnga_gui_wp_607a_native_gui_gate_tests"
CORPUS_TEST = "wp607c_generate_corpus"

# L2/L3 binding lessons from WP-5U14N: a 30s -functions platform probe before
# captures, a 300s hard gate timeout on the DIRECT test binary invocation
# (never through ctest) with streamed logs, so partial output survives a kill
# and a Qt platform-init hang is distinguishable from a test-logic hang.
PROBE_TIMEOUT_SECONDS = 30
GATE_TIMEOUT_SECONDS = 300
GATE_UNIT = "wp607a-native-gui-gate"

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

CORPUS_REVISION = "5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f"
RESULTS = ("PASS", "BLOCKED", "FAIL")
DRAFT_RESULT = "UNREVIEWED"
OUT_OF_SCOPE_IDS = ("statistics-export", "apng-timeline")

MANUAL_ROWS = (
    ("M01", "M01-open-dragdrop",
     "Native File Open and pointer drag/drop both work; rejection feedback "
     "is understandable"),
    ("M02", "M02-keyboard-focus",
     "keyboard-only core workflows complete; focus order and visible focus "
     "are logical; no trap"),
    ("M03", "M03-docks-scale",
     "pointer dock drag/float/redock/reset works at every required scale "
     "without clipping"),
    ("M04", "M04-screen-reader",
     "Narrator/VoiceOver/Orca announces control name, role, state/value and "
     "meaningful selection/status changes"),
    ("M05", "M05-clipboard",
     "native copy/paste works for a representative value without unexpected "
     "formatting or state changes"),
    ("M06", "M06-lifecycle",
     "close, reopen and rapid switching show no stale image, Chunk, stage, "
     "selection, trace or announcement"),
)

# Frozen native scale matrix (package "Native scale matrix").
SCALE_ROWS = {
    "windows-x64": ("100%", "150%", "200%"),
    "macos-arm64": ("retina-native", "logical-scaled"),
    "ubuntu-lts-x64": ("100%", "150%", "200%"),
}

REQUIRED_RECORD_FIELDS = (
    "schema", "schema_version", "work_package", "platform",
    "git_commit", "command", "os_build", "architecture", "compiler",
    "qt_version", "qt_platform_plugin", "display_session", "logical_dpi",
    "device_pixel_ratio", "cpu", "memory", "machine_label",
    "corpus_revision", "fixtures", "utc_timestamp", "status",
    "out_of_scope", "cells",
)

REQUIRED_CELL_FIELDS = (
    "id", "form", "fixture_id", "fixture_sha256", "expected", "result",
    "note",
)

TIMESTAMP_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
SHA_PATTERN = re.compile(r"^[0-9a-f]{64}$")
WINDOWS_ABSOLUTE_PATTERN = re.compile(r"^([A-Za-z]:[\\/]|\\\\)")
PRIVACY_KEYS = frozenset({
    "username", "hostname", "host_name", "clipboard", "clipboard_text",
    "audio", "screen_reader_audio",
})


class Refused(Exception):
    """A frozen precondition failed; the runner must not capture."""


def validate_record(record, expected_platform):
    """Validates one pnga-wp607a-native-gui-v1 record or raises Refused."""
    if expected_platform not in NATIVE_PLUGINS:
        raise Refused(f"unknown platform id {expected_platform!r}")
    if not isinstance(record, dict):
        raise Refused("record is not a JSON object")

    string_fields = tuple(field for field in REQUIRED_RECORD_FIELDS
                          if field not in ("schema_version", "out_of_scope",
                                           "fixtures", "cells"))
    for field in REQUIRED_RECORD_FIELDS:
        if field not in record:
            raise Refused(f"missing required field {field}")
    for field in string_fields:
        value = record[field]
        if not isinstance(value, str) or not value.strip():
            raise Refused(f"field {field} must be a non-empty string")
    if record["schema"] != SCHEMA:
        raise Refused("schema mismatch")
    if record["schema_version"] != SCHEMA_VERSION:
        raise Refused("schema_version mismatch")
    if record["work_package"] != WORK_PACKAGE:
        raise Refused("work_package mismatch")
    if record["platform"] != expected_platform:
        raise Refused(f"platform {record['platform']!r} does not match the "
                      f"requested platform {expected_platform!r}")
    if not TIMESTAMP_PATTERN.match(record["utc_timestamp"]):
        raise Refused("utc_timestamp must be a UTC Z timestamp")
    if record["status"] not in RESULTS:
        raise Refused(f"record status {record['status']!r} is outside the "
                      "frozen PASS/BLOCKED/FAIL vocabulary")
    plugins = NATIVE_PLUGINS[expected_platform]
    if record["qt_platform_plugin"] not in plugins:
        raise Refused(f"qt_platform_plugin {record['qt_platform_plugin']!r} "
                      f"is not a native plugin for {expected_platform}; "
                      "offscreen/minimal runs can never satisfy a cell")
    if record["corpus_revision"] != CORPUS_REVISION:
        raise Refused("corpus_revision does not match the frozen WP-607C "
                      "revision")
    fixtures = record["fixtures"]
    if not isinstance(fixtures, dict) or set(fixtures) != set(FIXTURE_IDS):
        raise Refused("fixtures must carry exactly the five frozen WP-607C "
                      "fixture ids")
    for fixture_id, digest in fixtures.items():
        if not isinstance(digest, str) or not SHA_PATTERN.match(digest):
            raise Refused(f"fixture {fixture_id} sha256 must be 64 lowercase "
                          "hex digits")
    out_of_scope = record["out_of_scope"]
    if not isinstance(out_of_scope, list):
        raise Refused("out_of_scope must be a list")
    for entry in OUT_OF_SCOPE_IDS:
        if entry not in out_of_scope:
            raise Refused(f"out_of_scope must declare {entry}")

    cells = record["cells"]
    if not isinstance(cells, list):
        raise Refused("cells must be a list")
    seen = set()
    for index, cell in enumerate(cells):
        if not isinstance(cell, dict):
            raise Refused(f"cells[{index}] is not an object")
        for field in REQUIRED_CELL_FIELDS:
            if field not in cell:
                raise Refused(f"cells[{index}] is missing {field}")
            value = cell[field]
            if not isinstance(value, str) or not value.strip():
                raise Refused(f"cells[{index}].{field} must be a non-empty "
                              "string")
        if cell["id"] not in AUTOMATED_CELLS:
            raise Refused(f"unknown automated cell {cell['id']!r}")
        if cell["id"] in seen:
            raise Refused(f"duplicate automated cell {cell['id']}")
        seen.add(cell["id"])
        if cell["form"] != "automated":
            raise Refused(f"cell {cell['id']} form must be automated")
        if cell["result"] not in RESULTS:
            raise Refused(f"cell {cell['id']} result {cell['result']!r} is "
                          "outside the frozen vocabulary")
        if cell["fixture_id"] not in FIXTURE_IDS:
            raise Refused(f"cell {cell['id']} uses unknown fixture "
                          f"{cell['fixture_id']!r}")
        if fixtures[cell["fixture_id"]] != cell["fixture_sha256"]:
            raise Refused(f"cell {cell['id']} fixture_sha256 does not match "
                          "the corpus registry")
    missing = [cell_id for cell_id in AUTOMATED_CELLS if cell_id not in seen]
    if missing:
        raise Refused(f"missing automated cells {missing}")

    key_offenders = privacy_offenders(record)
    if key_offenders:
        raise Refused(f"privacy-key violation at {key_offenders[:3]}")
    path_offenders = absolute_path_strings(record)
    if path_offenders:
        raise Refused(f"absolute path(s) present: {path_offenders[:3]}")


def absolute_path_strings(value):
    """Every string anywhere in `value` shaped like an absolute path."""
    offenders = []
    if isinstance(value, str):
        if value.startswith("/") or WINDOWS_ABSOLUTE_PATTERN.match(value):
            offenders.append(value)
    elif isinstance(value, dict):
        for entry in value.values():
            offenders.extend(absolute_path_strings(entry))
    elif isinstance(value, (list, tuple)):
        for entry in value:
            offenders.extend(absolute_path_strings(entry))
    return offenders


def privacy_offenders(value, prefix=""):
    """Key paths anywhere in `value` whose key carries personal data."""
    offenders = []
    if isinstance(value, dict):
        for key, entry in value.items():
            path = f"{prefix}.{key}" if prefix else str(key)
            if str(key).lower() in PRIVACY_KEYS:
                offenders.append(path)
            else:
                offenders.extend(privacy_offenders(entry, path))
    elif isinstance(value, (list, tuple)):
        for index, entry in enumerate(value):
            offenders.extend(privacy_offenders(entry, f"{prefix}[{index}]"))
    return offenders


def serialize_record(record):
    """Compact sorted-key JSON, ASCII escaping, one trailing LF."""
    return json.dumps(record, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":")) + "\n"


def manual_template(platform_id):
    """The frozen M01-M06 plus scale-row draft template for `platform_id`."""
    if platform_id not in NATIVE_PLUGINS:
        raise Refused(f"unknown platform id {platform_id!r}")
    rows = []
    for cell_id, cell_name, observation in MANUAL_ROWS:
        rows.append({
            "id": cell_id,
            "cell": cell_name,
            "form": "manual",
            "category": "manual-cell",
            "observation_required": observation,
            "result": DRAFT_RESULT,
            "reviewer": "",
            "utc_time": "",
            "observation": "",
        })
    for label in SCALE_ROWS[platform_id]:
        rows.append({
            "id": f"scale-{platform_id}-{label}",
            "form": "manual",
            "category": "scale",
            "scale": label,
            "observation_required": "logical DPI, DPR, window size and "
                                    "usable text, focus rings, menus, docks "
                                    "and the three core workflows at this "
                                    "scale",
            "result": DRAFT_RESULT,
            "reviewer": "",
            "utc_time": "",
            "observation": "",
        })
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "work_package": WORK_PACKAGE,
        "platform": platform_id,
        "form": "manual-template",
        "draft": True,
        "rows": rows,
    }


# --- bounded native orchestration (Tasks 4-6 entry points) -------------------

HOST_SYSTEM = {"windows-x64": "Windows", "macos-arm64": "Darwin",
               "ubuntu-lts-x64": "Linux"}


def out_root(platform_id):
    return f"build/evidence/wp-607a/{platform_id}"


def corpus_dir(preset):
    return ROOT / "build" / preset / "tests" / "corpus" / "wp-607c"


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git_commit():
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                            check=True, capture_output=True, text=True)
    return result.stdout.strip()


def plan_commands(platform_id, preset, jobs):
    """The exact, order-pinned gate plan (relative paths only).

    The plan carries the relative output root and test binary; the executor
    resolves both against the repository root so records never embed an
    absolute path. The gate runs the test binary DIRECTLY under a hard 300s
    timeout with streamed logs (not through ctest) so partial output survives
    a kill and a Qt platform-level hang (the 30s -functions probe) is
    distinguishable from a test-logic hang. The corpus fixture runs as one
    explicit bounded ctest step before the probe and capture, replacing the
    implicit FIXTURES_REQUIRED dependency. The macOS plan alone wraps the
    capture in enable-fka/restore-fka (Full Keyboard Access flip with
    read-back verified save/restore, re-execution ruling 2). Schema
    validation and SHA assembly are runner-internal plan steps after the
    capture.
    """
    out = out_root(platform_id)
    binary = (f"build/{preset}/tests/gui/{GATE_TARGET}"
              + (".exe" if platform_id == "windows-x64" else ""))
    command = (f"python3 scripts/run_wp_607a_native_gui_gate.py --platform "
               f"{platform_id} --preset {preset} --jobs {jobs}")
    gate_env = {
        "PNGA_WP607A_OUT": out,
        "PNGA_WP607A_PLATFORM": platform_id,
        "PNGA_WP607A_COMMAND": command,
    }
    steps = [
        {"kind": "build",
         "command": ["cmake", "--build", "--preset", preset, "--target",
                     GATE_TARGET, "--parallel", str(jobs)],
         "env": None},
        {"kind": "fixture",
         "command": ["ctest", "--preset", preset, "-R", CORPUS_TEST,
                     "--output-on-failure", "--timeout", "120"],
         "env": None},
        {"kind": "probe", "command": [binary, "-functions"],
         "env": dict(gate_env)},
    ]
    # Re-execution ruling 2: macOS gates Tab traversal behind Full Keyboard
    # Access (OFF by default), so the runner flips FKA on for the capture
    # and restores the saved host state with read-back verification. The
    # restore-fka step runs on the success path; the try/finally in the
    # executor covers every failure path after the enable.
    if platform_id == "macos-arm64":
        steps.append({"kind": "enable-fka", "unit": FKA_UNIT})
    steps.append(
        {"kind": "capture", "unit": GATE_UNIT,
         "command": [binary, "-o", f"{out}/logs/qtest-{GATE_UNIT}.txt,txt"],
         "env": dict(gate_env)})
    if platform_id == "macos-arm64":
        steps.append({"kind": "restore-fka", "unit": FKA_UNIT})
    steps.extend([
        {"kind": "validate", "unit": "automated.json"},
        {"kind": "assemble", "unit": "evidence.json"},
    ])
    return steps


def run(command, *, capture=False, env=None):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=str(ROOT), check=False, text=True,
                          capture_output=capture, env=env)


def run_bounded(command, log_path, *, timeout, env):
    """Run `command` under a hard `timeout`, streaming stdout+stderr into
    `log_path` as the child produces it. The child is killed on expiry;
    partial output survives in the log. Returns the exit code, or None
    when the timeout killed the child."""
    print("$ " + " ".join(command), flush=True)
    Path(log_path).parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "wb") as handle:
        try:
            completed = subprocess.run(command, cwd=str(ROOT), check=False,
                                       env=env, stdout=handle,
                                       stderr=subprocess.STDOUT,
                                       timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f"timed out after {timeout}s; child killed; partial "
                  "output kept in the streamed log", flush=True)
            return None
    return completed.returncode


def qtest_log_tail(out_dir, unit, lines=15):
    """The tail of the unit's qtest log (or a placeholder when absent)."""
    path = Path(out_dir) / "logs" / f"qtest-{unit}.txt"
    if not path.is_file():
        return "(no qtest log was written)"
    content = path.read_text(encoding="utf-8", errors="replace").strip()
    if not content:
        return "(qtest log is empty)"
    return "\n".join(content.splitlines()[-lines:])


def qt_runtime_dir_from_cache(cache_path):
    """L1 (WP-5U14N CI lesson): the Qt runtime directory derived from a
    CMakeCache.txt, or None.

    Parses the `Qt6_DIR:PATH=<prefix>/lib/cmake/Qt6` entry; the runtime dir
    is `<prefix>/bin` on Windows and `<prefix>/lib` elsewhere (harmless to
    prepend). A missing cache file or entry means no Qt kit in that build
    tree; callers skip the PATH prepend silently (hosts resolving Qt through
    rpath keep working).
    """
    try:
        text = Path(cache_path).read_text(encoding="utf-8",
                                           errors="replace")
    except OSError:
        return None
    match = re.search(r"^Qt6_DIR:PATH=(.+)$", text, re.MULTILINE)
    if not match:
        return None
    prefix = Path(match.group(1).strip()).parent.parent.parent
    return prefix / ("bin" if host_platform.system() == "Windows" else "lib")


def child_environment(step_env, qt_dir):
    """The child process environment: the plan's relative PNGA_WP607A_OUT is
    resolved against the repository root and the Qt runtime dir is prepended
    to PATH (L1) so probe/capture children find the Qt runtime on Windows."""
    env = dict(os.environ)
    for name, value in (step_env or {}).items():
        env[name] = str(ROOT / value) if name == "PNGA_WP607A_OUT" else value
    if qt_dir is not None:
        env["PATH"] = str(qt_dir) + os.pathsep + env.get("PATH", "")
    return env


def install_termination_cleanup():
    """SIGTERM/SIGBREAK cleanup: the exit unwinds through subprocess.run,
    which kills the running child so no native capture survives the runner."""
    def handler(signum, _frame):
        raise SystemExit(128 + signum)
    for name in ("SIGTERM", "SIGBREAK"):
        sig = getattr(signal, name, None)
        if sig is not None:
            signal.signal(sig, handler)


# --- frozen preflight refusals -------------------------------------------------

def windows_session_ok(session_name):
    """A Windows native desktop needs the interactive Console session;
    RDP/service sessions that suppress native desktop interaction refuse."""
    return session_name.startswith("Console")


def macos_windowserver_ok(env):
    """A native cocoa run needs a logged-in WindowServer session.

    Note (macOS 26): launchd no longer exports SECURITYSESSIONID into the
    environment of GUI processes at all, so this check accepts any real
    Aqua/WindowServer session evidence — such as the genuine session id
    restored from Security.framework SessionGetInfo by the invoking
    environment (re-execution ruling 4: no behavior change).
    """
    return bool(env.get("SECURITYSESSIONID"))


def linux_display_ok(env):
    """A native Ubuntu run needs an X11 or Wayland session socket."""
    return bool(env.get("DISPLAY") or env.get("WAYLAND_DISPLAY"))


# --- macOS Full Keyboard Access flip (re-execution ruling 2) -------------------
#
# macOS gates Tab traversal to buttons/checkboxes behind Full Keyboard
# Access, which is OFF by default, so the A05 keyboard-only walk cannot
# reach lockCoordinate at host defaults. The runner enables FKA for the
# gate with the same save/restore discipline as the WP-5U14N appearance
# flips: save -> write -> read-back verified -> restore -> read-back
# verified, and any restore/write mismatch is REFUSED, never a PASS.

FKA_UNIT = "fka-AppleKeyboardUIMode"
FKA_DOMAIN = "NSGlobalDomain"
FKA_KEY = "AppleKeyboardUIMode"
FKA_ENABLED_VALUE = 3


def fka_read_command():
    return ["defaults", "read", FKA_DOMAIN, FKA_KEY]


def fka_write_command(value):
    return ["defaults", "write", FKA_DOMAIN, FKA_KEY, "-int", str(value)]


def fka_delete_command():
    return ["defaults", "delete", FKA_DOMAIN, FKA_KEY]


def fka_value_from_output(text):
    """Parses a `defaults read` value: an integer, or None when the key is
    absent (a failed read) or holds a non-integer value."""
    text = (text or "").strip()
    return int(text) if text.isdigit() else None


def fka_current_value():
    result = subprocess.run(fka_read_command(), cwd=str(ROOT), check=False,
                            capture_output=True, text=True, timeout=15)
    if result.returncode != 0:
        return None
    return fka_value_from_output(result.stdout)


def run_fka_command(command):
    return subprocess.run(command, cwd=str(ROOT), check=False,
                          capture_output=True, text=True, timeout=15)


def enable_fka():
    """Saves the current FKA value, enables Full Keyboard Access for the
    gate and verifies the write by reading back. Refused on any mismatch,
    after a best-effort restore of the saved value."""
    saved = fka_current_value()
    run_fka_command(fka_write_command(FKA_ENABLED_VALUE))
    if fka_current_value() != FKA_ENABLED_VALUE:
        try:
            restore_fka(saved)
        except Refused:
            pass
        raise Refused(
            f"{FKA_KEY} write did not read back as {FKA_ENABLED_VALUE}; the "
            "macOS gate refuses to capture without verified FKA state")
    return saved


def restore_fka(saved):
    """Restores the saved FKA value (None = key was absent, so delete) and
    verifies by reading back; Refused on mismatch."""
    command = (fka_delete_command() if saved is None
               else fka_write_command(saved))
    run_fka_command(command)
    actual = fka_current_value()
    if actual != saved:
        raise Refused(
            f"{FKA_KEY} restore mismatch: expected {saved!r}, read back "
            f"{actual!r}; the host keyboard-access state must be restored "
            "exactly (appearance save/restore discipline)")


def xvfb_marker_in(process_list):
    """The Xvfb marker is the server process name in the process list."""
    return "Xvfb" in process_list


def xvfb_marker_present():
    if host_platform.system() != "Linux":
        return False
    try:
        result = subprocess.run(["ps", "-e", "-o", "args="],
                                capture_output=True, text=True, timeout=10,
                                check=False)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return xvfb_marker_in(result.stdout or "")


def preflight(platform_id):
    if platform_id not in NATIVE_PLUGINS:
        raise Refused(f"unknown platform id {platform_id!r}")
    if host_platform.system() != HOST_SYSTEM[platform_id]:
        raise Refused(
            f"--platform {platform_id} requires running on "
            f"{HOST_SYSTEM[platform_id]}; native WP-607A evidence cannot be "
            "produced on this host")
    requested = os.environ.get("QT_QPA_PLATFORM")
    if requested in ("offscreen", "minimal"):
        raise Refused(
            f"QT_QPA_PLATFORM={requested} cannot satisfy a native WP-607A "
            "cell (R9); unset it and rerun on the real desktop")
    if platform_id == "windows-x64":
        session = os.environ.get("SESSIONNAME", "")
        if not windows_session_ok(session):
            raise Refused(
                f"missing Windows interactive session (SESSIONNAME="
                f"{session!r}); a native windows-plugin run needs the "
                "interactive Console desktop")
    if platform_id == "macos-arm64":
        if not macos_windowserver_ok(os.environ):
            raise Refused(
                "no macOS WindowServer session (SECURITYSESSIONID is unset);"
                " a native cocoa run needs a logged-in desktop session")
    if platform_id == "ubuntu-lts-x64":
        if not linux_display_ok(os.environ):
            raise Refused(
                "neither DISPLAY nor WAYLAND_DISPLAY is set; a native "
                "Ubuntu run needs an X11 or Wayland session")
        if xvfb_marker_present():
            raise Refused(
                "Xvfb-only session detected; Xvfb cannot satisfy a native "
                "WP-607A cell (R3)")


def refuse_dirty_output_dir(out_dir):
    """Refuses any pre-existing native evidence in the platform output dir."""
    stale = []
    for name in ("automated.json", "evidence.json", "manual.json",
                 "manual-template.json"):
        if (Path(out_dir) / name).exists():
            stale.append(name)
    logs = Path(out_dir) / "logs"
    if logs.is_dir() and any(logs.iterdir()):
        stale.append("logs/*")
    if stale:
        raise Refused(
            "output directory is dirty; remove "
            f"{out_root('')}{Path(out_dir).name} and rerun (found "
            f"{', '.join(stale[:5])}{'…' if len(stale) > 5 else ''})")


def load_corpus_registry(preset):
    """Pinned fixture id -> expected sha256 from the generated catalog."""
    catalog_path = corpus_dir(preset) / "index.json"
    if not catalog_path.is_file():
        raise Refused(
            f"missing corpus catalog {catalog_path.relative_to(ROOT)}; build "
            f"the {CORPUS_TEST} fixture first")
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    if catalog.get("corpus_revision") != CORPUS_REVISION:
        raise Refused("corpus_revision does not match the frozen WP-607C "
                      "revision")
    registry = {case["id"]: (case["output"], case["expected_sha256"])
                for case in catalog.get("cases", [])}
    missing = [fixture_id for fixture_id in FIXTURE_IDS
               if fixture_id not in registry]
    if missing:
        raise Refused(f"corpus catalog lacks pinned fixture ids {missing}")
    hashes = {}
    for fixture_id in FIXTURE_IDS:
        output, expected = registry[fixture_id]
        fixture_path = corpus_dir(preset) / output
        if not fixture_path.is_file():
            raise Refused(
                f"missing corpus fixture {fixture_id}; build the "
                f"{CORPUS_TEST} fixture first")
        actual = sha256_file(fixture_path)
        if actual != expected:
            raise Refused(
                f"corpus fixture {fixture_id} bytes do not match the "
                "registry sha256")
        hashes[fixture_id] = actual
    return hashes


def validate_and_load_record(out_dir, platform_id, fixture_hashes):
    """Schema-validates the gate's automated.json against the frozen
    contract and cross-checks every fixture hash with the freshly computed
    corpus registry."""
    path = Path(out_dir) / "automated.json"
    if not path.is_file():
        raise Refused(
            "the gate wrote no automated.json; the QtTest run did not "
            "complete its A01-A11 cells")
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise Refused(f"automated.json is not valid JSON: {error}")
    validate_record(record, platform_id)
    for fixture_id in FIXTURE_IDS:
        if record["fixtures"][fixture_id] != fixture_hashes[fixture_id]:
            raise Refused(
                f"record fixture {fixture_id} sha256 does not match the "
                "generated corpus")
    return record


def write_evidence(out_dir, evidence):
    """Atomic evidence write: canonical serialization into a temporary file,
    then one rename (never a partially visible evidence.json)."""
    offenders = absolute_path_strings(evidence)
    if offenders:
        raise SystemExit(
            "WP-607A evidence would contain absolute path(s) "
            f"{offenders[:3]}; refusing to write")
    output = Path(out_dir) / "evidence.json"
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(serialize_record(evidence), encoding="utf-8")
    os.replace(temporary, output)


def validate_final_record(record):
    """Aggregate validation (closure entries): only final results are
    accepted, and a final PASS refuses until every manual row is PASS with
    reviewer, UTC time and a non-empty semantic observation. The UNREVIEWED
    draft state is permitted only in an unsubmitted template."""
    if not isinstance(record, dict):
        raise Refused("record is not a JSON object")
    cells = record.get("cells")
    if not isinstance(cells, list):
        raise Refused("cells must be a list")
    for row in cells:
        if not isinstance(row, dict) or row.get("form") != "manual":
            continue
        result = row.get("result")
        if result == DRAFT_RESULT:
            raise Refused(f"manual row {row.get('id')!r} is still "
                          f"{DRAFT_RESULT}; a submitted record cannot carry "
                          "drafts")
        if result not in RESULTS:
            raise Refused(f"manual row {row.get('id')!r} result "
                          f"{result!r} is outside the frozen vocabulary")
        if result == "PASS":
            for field in ("reviewer", "utc_time", "observation"):
                value = row.get(field)
                if not isinstance(value, str) or not value.strip():
                    raise Refused(f"manual row {row.get('id')!r} PASS "
                                  f"requires a non-empty {field}")
            if not TIMESTAMP_PATTERN.match(row.get("utc_time", "")):
                raise Refused(f"manual row {row.get('id')!r} utc_time must "
                              "be a UTC Z timestamp")
    if record.get("status") == "PASS":
        for row in cells:
            if isinstance(row, dict) and row.get("form") == "manual" \
                    and row.get("result") != "PASS":
                raise Refused(f"final PASS refused: manual row "
                              f"{row.get('id')!r} is {row.get('result')!r}, "
                              "not PASS")
    key_offenders = privacy_offenders(record)
    if key_offenders:
        raise Refused(f"privacy-key violation at {key_offenders[:3]}")
    path_offenders = absolute_path_strings(record)
    if path_offenders:
        raise Refused(f"absolute path(s) present: {path_offenders[:3]}")


def run_gate(platform_id, preset, jobs):
    """The bounded native gate: build, corpus fixture, platform probe, direct
    bounded capture, schema validation, SHA assembly (order-pinned)."""
    install_termination_cleanup()
    preflight(platform_id)
    out_dir = ROOT / out_root(platform_id)
    refuse_dirty_output_dir(out_dir)
    fixture_hashes = load_corpus_registry(preset)
    steps = plan_commands(platform_id, preset, jobs)
    # L1: derive the Qt runtime dir once per run from the build-tree cache
    # and prepend it to PATH for the probe/capture children (a Windows child
    # fails with 0xC0000135 STATUS_DLL_NOT_FOUND otherwise).
    qt_dir = qt_runtime_dir_from_cache(ROOT / "build" / preset /
                                       "CMakeCache.txt")
    record = None
    saved_fka = None
    try:
        for step in steps:
            kind = step["kind"]
            if kind in ("build", "fixture"):
                result = run(step["command"])
                if result.returncode != 0:
                    raise SystemExit(
                        f"WP-607A gate: FAIL at {kind} step"
                        + (f" ({CORPUS_TEST})" if kind == "fixture" else "")
                        + "; no evidence assembled")
            elif kind == "enable-fka":
                saved_fka = enable_fka()
            elif kind == "restore-fka":
                restore_fka(saved_fka)
                saved_fka = None
            elif kind in ("probe", "capture"):
                env = child_environment(step["env"], qt_dir)
                unit = step.get("unit", kind)
                log_name = ("probe-functions.log" if kind == "probe"
                            else f"run-{unit}.log")
                timeout = (PROBE_TIMEOUT_SECONDS if kind == "probe"
                           else GATE_TIMEOUT_SECONDS)
                code = run_bounded(step["command"], out_dir / "logs" /
                                   log_name, timeout=timeout, env=env)
                if code is None:
                    detail = ("the Qt platform itself cannot initialize"
                              if kind == "probe" else
                              "no evidence assembled. qtest log tail:\n"
                              + qtest_log_tail(out_dir, unit))
                    raise SystemExit(
                        f"WP-607A gate: FAIL at {kind} step ({unit} timed "
                        f"out after {timeout}s; child killed; {detail})")
                if code != 0:
                    raise SystemExit(
                        f"WP-607A gate: FAIL at {kind} step ({unit} exited "
                        f"{code}); no evidence assembled. qtest log tail:\n"
                        + qtest_log_tail(out_dir, unit))
            elif kind == "validate":
                record = validate_and_load_record(out_dir, platform_id,
                                                  fixture_hashes)
                print(f"WP-607A gate: validated {len(record['cells'])} "
                      f"automated cells ({record['status']})", flush=True)
            elif kind == "assemble":
                if record is None:
                    raise SystemExit("WP-607A gate: assembly ran before "
                                     "validation; refusing to write "
                                     "evidence")
                artifacts = {
                    "automated.json": sha256_file(out_dir / "automated.json"),
                }
                evidence = {
                    "schema": SCHEMA,
                    "work_package": WORK_PACKAGE,
                    "status": record["status"],
                    "platform": platform_id,
                    "preset": preset,
                    "commit": git_commit(),
                    "time_utc": utc_now(),
                    "corpus_revision": CORPUS_REVISION,
                    "fixtures": fixture_hashes,
                    "out_of_scope": list(OUT_OF_SCOPE_IDS),
                    "artifacts": artifacts,
                    "cells": record["cells"],
                }
                write_evidence(out_dir, evidence)
            else:
                raise SystemExit(f"WP-607A gate: unknown plan step {kind!r}")
    finally:
        # Re-execution ruling 2: after the FKA enable, EVERY exit path —
        # capture failure, timeout, signal, validation refusal — restores
        # the saved host keyboard-access state with read-back verification
        # (appearance save/restore discipline). The restore-fka plan step
        # already ran on the success path, so the finally is a no-op there.
        if saved_fka is not None:
            restore_fka(saved_fka)
            saved_fka = None
    status = record["status"] if record is not None else "FAIL"
    print(f"WP-607A native gui gate: {status} evidence="
          f"{out_root(platform_id)}/evidence.json")
    return 0 if status == "PASS" else 1


def write_manual_template(platform_id):
    if platform_id not in NATIVE_PLUGINS:
        raise Refused(f"unknown platform id {platform_id!r}")
    out_dir = ROOT / out_root(platform_id)
    template_path = out_dir / "manual-template.json"
    if template_path.exists():
        raise Refused(
            f"manual template already exists at "
            f"{out_root(platform_id)}/manual-template.json; delete it "
            "explicitly to regenerate")
    out_dir.mkdir(parents=True, exist_ok=True)
    template = manual_template(platform_id)
    temporary = template_path.with_suffix(".json.tmp")
    temporary.write_text(serialize_record(template), encoding="utf-8")
    os.replace(temporary, template_path)
    print(f"WP-607A manual template for {platform_id}: "
          f"{len(template['rows'])} rows (M01-M06 plus scale) written to "
          f"{out_root(platform_id)}/manual-template.json as {DRAFT_RESULT}")
    return 0


def run_self_test():
    """Self-asserts the frozen schema/refusal contract; writes nothing."""
    import copy

    failures = []

    def expect_refused(broken, platform, label):
        try:
            validate_record(broken, platform)
        except Refused:
            return
        failures.append(f"{label} was not refused")

    record = synthetic_valid_record()
    try:
        validate_record(record, "macos-arm64")
    except Refused as error:
        failures.append(f"valid synthetic record rejected: {error}")

    for field in REQUIRED_RECORD_FIELDS:
        broken = copy.deepcopy(record)
        broken.pop(field, None)
        expect_refused(broken, "macos-arm64", f"missing host field {field}")

    duplicated = copy.deepcopy(record)
    duplicated["cells"] = list(duplicated["cells"]) + \
        [copy.deepcopy(duplicated["cells"][0])]
    expect_refused(duplicated, "macos-arm64", "duplicate cell")

    offscreen = copy.deepcopy(record)
    offscreen["qt_platform_plugin"] = "offscreen"
    expect_refused(offscreen, "macos-arm64", "offscreen plugin substitution")

    absolute = copy.deepcopy(record)
    absolute["machine_label"] = "/Users/example/build"
    expect_refused(absolute, "macos-arm64", "absolute path in machine label")

    hostname = copy.deepcopy(record)
    hostname["hostname"] = "desktop.example.invalid"
    expect_refused(hostname, "macos-arm64", "hostname key")

    revision = copy.deepcopy(record)
    revision["corpus_revision"] = "0" * 64
    expect_refused(revision, "macos-arm64", "changed corpus revision")

    unknown_fixture = copy.deepcopy(record)
    unknown_fixture["cells"][2]["fixture_id"] = "unrecorded-local-image"
    expect_refused(unknown_fixture, "macos-arm64", "unknown fixture")

    for entry in OUT_OF_SCOPE_IDS:
        missing_scope = copy.deepcopy(record)
        missing_scope["out_of_scope"] = [
            item for item in record["out_of_scope"] if item != entry]
        expect_refused(missing_scope, "macos-arm64",
                       f"missing out_of_scope entry {entry}")

    mismatched = copy.deepcopy(record)
    mismatched["platform"] = "windows-x64"
    expect_refused(mismatched, "macos-arm64", "platform mismatch")

    unknown_platform = copy.deepcopy(record)
    expect_refused(unknown_platform, "unknown-platform", "unknown platform")

    bad_result = copy.deepcopy(record)
    bad_result["cells"][0]["result"] = "NOT_CONFIGURED"
    expect_refused(bad_result, "macos-arm64", "out-of-vocabulary result")

    first = serialize_record(record)
    second = serialize_record(record)
    if first != second:
        failures.append("record serialization is not deterministic")
    if not first.endswith("\n") or first.endswith("\n\n"):
        failures.append("record serialization must end with exactly one LF")
    if not first.isascii():
        failures.append("record serialization must be ASCII-only")
    if first != json.dumps(record, sort_keys=True, ensure_ascii=True,
                           separators=(",", ":")) + "\n":
        failures.append("record serialization must be compact and sorted")
    try:
        if json.loads(first)["schema"] != SCHEMA:
            failures.append("serialized record does not round-trip")
    except ValueError:
        failures.append("serialized record is not valid JSON")

    try:
        manual_template("unknown-platform")
        failures.append("manual template accepted an unknown platform")
    except Refused:
        pass
    template = manual_template("macos-arm64")
    row_ids = [row["id"] for row in template["rows"]]
    if row_ids != list(MANUAL_CELLS) + [
            f"scale-macos-arm64-{label}" for label in
            SCALE_ROWS["macos-arm64"]]:
        failures.append(f"manual template rows drift: {row_ids}")
    if any(row["result"] != DRAFT_RESULT for row in template["rows"]):
        failures.append("manual template rows must start as UNREVIEWED")
    if any(row["reviewer"] or row["observation"] for row in template["rows"]):
        failures.append("manual template must exclude personal fields")

    # The gate plan must equal the pinned order exactly: build target ->
    # bounded wp607c corpus fixture -> native platform probe -> (macOS only)
    # the FKA enable -> one direct bounded gate invocation -> (macOS only)
    # the FKA restore -> schema validation -> SHA assembly.
    macos_binary = "build/dev/tests/gui/pnga_gui_wp_607a_native_gui_gate_tests"
    windows_binary = macos_binary + ".exe"
    macos_gate_env = {
        "PNGA_WP607A_OUT": "build/evidence/wp-607a/macos-arm64",
        "PNGA_WP607A_PLATFORM": "macos-arm64",
        "PNGA_WP607A_COMMAND": ("python3 scripts/run_wp_607a_native_gui_gate"
                                ".py --platform macos-arm64 --preset dev "
                                "--jobs 4"),
    }
    expected_macos = [
        {"kind": "build",
         "command": ["cmake", "--build", "--preset", "dev", "--target",
                     GATE_TARGET, "--parallel", "4"],
         "env": None},
        {"kind": "fixture",
         "command": ["ctest", "--preset", "dev", "-R", CORPUS_TEST,
                     "--output-on-failure", "--timeout", "120"],
         "env": None},
        {"kind": "probe", "command": [macos_binary, "-functions"],
         "env": dict(macos_gate_env)},
        {"kind": "enable-fka", "unit": FKA_UNIT},
        {"kind": "capture", "unit": GATE_UNIT,
         "command": [macos_binary, "-o",
                     "build/evidence/wp-607a/macos-arm64/logs/"
                     f"qtest-{GATE_UNIT}.txt,txt"],
         "env": dict(macos_gate_env)},
        {"kind": "restore-fka", "unit": FKA_UNIT},
        {"kind": "validate", "unit": "automated.json"},
        {"kind": "assemble", "unit": "evidence.json"},
    ]
    planned = plan_commands("macos-arm64", "dev", 4)
    if planned != expected_macos:
        failures.append(f"macos-arm64 gate plan drift: {planned}")

    windows_gate_env = dict(macos_gate_env,
                            PNGA_WP607A_OUT="build/evidence/wp-607a/windows-x64",
                            PNGA_WP607A_PLATFORM="windows-x64",
                            PNGA_WP607A_COMMAND=(
                                "python3 scripts/run_wp_607a_native_gui_gate"
                                ".py --platform windows-x64 --preset dev "
                                "--jobs 4"))
    expected_windows = [
        {"kind": "build",
         "command": ["cmake", "--build", "--preset", "dev", "--target",
                     GATE_TARGET, "--parallel", "4"],
         "env": None},
        {"kind": "fixture",
         "command": ["ctest", "--preset", "dev", "-R", CORPUS_TEST,
                     "--output-on-failure", "--timeout", "120"],
         "env": None},
        {"kind": "probe", "command": [windows_binary, "-functions"],
         "env": dict(windows_gate_env)},
        {"kind": "capture", "unit": GATE_UNIT,
         "command": [windows_binary, "-o",
                     "build/evidence/wp-607a/windows-x64/logs/"
                     f"qtest-{GATE_UNIT}.txt,txt"],
         "env": dict(windows_gate_env)},
        {"kind": "validate", "unit": "automated.json"},
        {"kind": "assemble", "unit": "evidence.json"},
    ]
    planned = plan_commands("windows-x64", "dev", 4)
    if planned != expected_windows:
        failures.append(f"windows-x64 gate plan drift: {planned}")

    # Preflight refusals: platform/host mismatches refuse on any host, and
    # the offscreen/minimal substitution refuses before any capture.
    for platform_id, host in (("windows-x64", "Windows"),
                              ("macos-arm64", "Darwin"),
                              ("ubuntu-lts-x64", "Linux")):
        if host_platform.system() != host:
            try:
                preflight(platform_id)
                failures.append(f"{platform_id} preflight accepted a "
                                f"non-{host} host")
            except Refused:
                pass
    try:
        preflight("unknown-platform")
        failures.append("unknown platform preflight not refused")
    except Refused:
        pass
    if host_platform.system() == "Darwin":
        saved_qpa = os.environ.get("QT_QPA_PLATFORM")
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        try:
            preflight("macos-arm64")
            failures.append("offscreen environment not refused")
        except Refused:
            pass
        os.environ["QT_QPA_PLATFORM"] = "minimal"
        try:
            preflight("macos-arm64")
            failures.append("minimal environment not refused")
        except Refused:
            pass
        finally:
            if saved_qpa is None:
                os.environ.pop("QT_QPA_PLATFORM", None)
            else:
                os.environ["QT_QPA_PLATFORM"] = saved_qpa

    # Session refusals (pure helpers, host-independent).
    if not windows_session_ok("Console"):
        failures.append("Windows Console session must be accepted")
    for session in ("", "RDP-Tcp#0", "Services"):
        if windows_session_ok(session):
            failures.append(f"Windows session {session!r} must refuse")
    if macos_windowserver_ok({}):
        failures.append("missing SECURITYSESSIONID must refuse")
    if not macos_windowserver_ok({"SECURITYSESSIONID": "x"}):
        failures.append("a SECURITYSESSIONID session must be accepted")
    if linux_display_ok({}):
        failures.append("unset DISPLAY+WAYLAND_DISPLAY must refuse")
    if not linux_display_ok({"DISPLAY": ":0"}):
        failures.append("an X11 display must be accepted")
    if not linux_display_ok({"WAYLAND_DISPLAY": "wayland-0"}):
        failures.append("a Wayland display must be accepted")

    # FKA flip helpers (re-execution ruling 2): exact commands and the pure
    # read parser; the real save/enable/restore flip is exercised only by a
    # native macOS run (subprocess-bound, host-independent self-test).
    if fka_read_command() != ["defaults", "read", "NSGlobalDomain",
                              "AppleKeyboardUIMode"]:
        failures.append("fka read command drift")
    if fka_write_command(3) != ["defaults", "write", "NSGlobalDomain",
                                "AppleKeyboardUIMode", "-int", "3"]:
        failures.append("fka write command drift")
    if fka_delete_command() != ["defaults", "delete", "NSGlobalDomain",
                                "AppleKeyboardUIMode"]:
        failures.append("fka delete command drift")
    for fka_text, fka_expected in (("3", 3), (" 2\n", 2),
                                   ("does not exist", None), ("", None),
                                   ("off", None)):
        if fka_value_from_output(fka_text) != fka_expected:
            failures.append(f"fka value parse drift for {fka_text!r}")
    if not xvfb_marker_in("/usr/bin/Xvfb :99 -screen 0 1024x768x24"):
        failures.append("the Xvfb marker was not detected")
    if xvfb_marker_in("/usr/libexec/Xorg :0 -auth /private/var/x"):
        failures.append("a real Xorg server must not be flagged as Xvfb")

    # Dirty output directory refusal.
    import tempfile
    with tempfile.TemporaryDirectory() as temporary:
        dirty = Path(temporary)
        (dirty / "automated.json").write_text("{}", encoding="utf-8")
        try:
            refuse_dirty_output_dir(dirty)
            failures.append("dirty output directory not refused")
        except Refused as error:
            # The relative hint must be well-formed (no doubled slash).
            if "//" in str(error):
                failures.append(f"dirty-dir hint has a doubled slash: {error}")
        (dirty / "automated.json").unlink()
        (dirty / "logs").mkdir()
        (dirty / "logs" / "run-wp607a.log").write_text("x", encoding="utf-8")
        try:
            refuse_dirty_output_dir(dirty)
            failures.append("dirty logs directory not refused")
        except Refused:
            pass
        (dirty / "logs" / "run-wp607a.log").unlink()
        (dirty / "logs").rmdir()
        try:
            refuse_dirty_output_dir(dirty)
        except Refused as error:
            failures.append(f"clean output directory refused: {error}")

    # Missing corpus catalog refusal (no writes; the preset is absent).
    try:
        load_corpus_registry("wp607a-self-test-missing-preset")
        failures.append("missing corpus catalog not refused")
    except Refused:
        pass

    # Qt runtime derivation (L1): a Qt6_DIR cache entry yields <prefix>/bin
    # (Windows) or <prefix>/lib elsewhere; a missing cache or entry is
    # skipped silently so rpath-only hosts keep working.
    runtime_subdir = "bin" if host_platform.system() == "Windows" else "lib"
    with tempfile.TemporaryDirectory() as temporary:
        cache = Path(temporary) / "CMakeCache.txt"
        cache.write_text("# some cache\n"
                         "Qt6_DIR:PATH=/qt-prefix/lib/cmake/Qt6\n",
                         encoding="utf-8")
        derived = qt_runtime_dir_from_cache(cache)
        if derived != Path("/qt-prefix") / runtime_subdir:
            failures.append(f"qt runtime derivation drift: {derived}")
        bare = Path(temporary) / "without-qt6.txt"
        bare.write_text("CMAKE_HOME_DIRECTORY:INTERNAL=/x\n",
                        encoding="utf-8")
        if qt_runtime_dir_from_cache(bare) is not None:
            failures.append("missing Qt6_DIR entry must be skipped")
        if qt_runtime_dir_from_cache(
                Path(temporary) / "absent-CMakeCache.txt") is not None:
            failures.append("missing CMakeCache must be skipped")

    # Bounded invocation mechanics (L2): a fast child exits 0, a hung child
    # is killed at the timeout (returns None) and its streamed log survives.
    with tempfile.TemporaryDirectory() as temporary:
        log_path = Path(temporary) / "logs" / "quick.log"
        code = run_bounded([sys.executable, "-c", "print('quick')"],
                           log_path, timeout=60, env=dict(os.environ))
        if code != 0 or not log_path.read_text().strip():
            failures.append(f"fast bounded child failed: code={code}")
        hung_log = Path(temporary) / "logs" / "hung.log"
        code = run_bounded([sys.executable, "-c", "import time; time.sleep(5)"],
                           hung_log, timeout=1, env=dict(os.environ))
        if code is not None:
            failures.append("a hung child must be killed at the timeout")
        if not hung_log.is_file():
            failures.append("the streamed log must survive a kill")

    # qtest log tail extraction.
    with tempfile.TemporaryDirectory() as temporary:
        out_dir = Path(temporary)
        if qtest_log_tail(out_dir, "missing") != \
                "(no qtest log was written)":
            failures.append("missing qtest log tail placeholder drift")
        logs = out_dir / "logs"
        logs.mkdir()
        (logs / f"qtest-{GATE_UNIT}.txt").write_text(
            "line-1\nline-2\nline-3\n", encoding="utf-8")
        tail = qtest_log_tail(out_dir, GATE_UNIT, lines=2)
        if tail != "line-2\nline-3":
            failures.append(f"qtest log tail drift: {tail!r}")

    # Aggregate (final) validation: drafts and incomplete manual rows refuse;
    # reviewed PASS rows with reviewer/UTC/observation close the aggregate.
    def final_case(label, mutate, should_pass):
        import copy as copy_module
        base = {
            "status": "PASS",
            "cells": [
                {"id": "A01", "form": "automated", "result": "PASS"},
                {"id": "M04", "form": "manual", "result": "PASS",
                 "reviewer": "product-owner", "utc_time":
                 "2026-09-05T00:00:00Z", "observation": "VoiceOver announced "
                 "control names, roles and selection changes"},
            ],
        }

        def fail_aggregate_with_blocked():
            record = copy_module.deepcopy(base)
            record["status"] = "FAIL"
            record["cells"][1]["result"] = "BLOCKED"
            return record

        if label == "FAIL aggregate with a reviewed BLOCKED manual row":
            mutated = fail_aggregate_with_blocked()
        else:
            mutated = mutate(copy_module.deepcopy(base))
        try:
            validate_final_record(mutated)
        except Refused as error:
            if should_pass:
                failures.append(f"{label} unexpectedly refused: {error}")
            return
        if not should_pass:
            failures.append(f"{label} was not refused")

    final_case("reviewed manual row", lambda r: r, True)
    final_case(
        "draft manual row",
        lambda r: r["cells"][1].update({"result": "UNREVIEWED",
                                        "reviewer": "", "utc_time": "",
                                        "observation": ""}),
        False)
    final_case(
        "manual row without reviewer",
        lambda r: r["cells"][1].update({"reviewer": ""}),
        False)
    final_case(
        "manual row without observation",
        lambda r: r["cells"][1].update({"observation": ""}),
        False)
    final_case(
        "manual row with bad utc_time",
        lambda r: r["cells"][1].update({"utc_time": "2026-09-05 00:00:00"}),
        False)
    final_case(
        "manual row outside vocabulary",
        lambda r: r["cells"][1].update({"result": "NOT_CONFIGURED"}),
        False)
    final_case(
        "final PASS with a BLOCKED manual row",
        lambda r: r["cells"][1].update({"result": "BLOCKED"}),
        False)
    final_case(
        "FAIL aggregate with a reviewed BLOCKED manual row",
        lambda r: r,
        True)
    final_case(
        "hostname in an aggregate",
        lambda r: r.update({"hostname": "desktop.example.invalid"}),
        False)
    final_case(
        "absolute path in an aggregate",
        lambda r: r["cells"][1].update({"observation": "/Users/x/note"}),
        False)

    if failures:
        for failure in failures:
            print(f"self-test FAIL: {failure}")
        print(f"WP-607A self-test: {len(failures)} failure(s)")
        return 1
    print("WP-607A self-test: PASS")
    print("self-test: schema/refusal contract, gate command plans (incl. "
          "the macOS FKA enable/restore pair), preflight refusals, corpus "
          "and dirty-dir gates, bounded invocation mechanics, Qt runtime "
          "derivation (L1), FKA command/parser helpers, manual template "
          "and aggregate validation all match the frozen contract")
    return 0


def synthetic_valid_record():
    """One valid record covering A01-A11 for schema self-test purposes."""
    fixtures = {fixture_id: f"{index:064x}"
                for index, fixture_id in enumerate(FIXTURE_IDS)}
    cells = []
    for index, cell_id in enumerate(AUTOMATED_CELLS):
        cells.append({
            "id": cell_id,
            "form": "automated",
            "fixture_id": FIXTURE_IDS[index % len(FIXTURE_IDS)],
            "fixture_sha256": fixtures[FIXTURE_IDS[index % len(FIXTURE_IDS)]],
            "expected": f"synthetic expected observation for {cell_id}",
            "result": "PASS",
            "note": f"synthetic note for {cell_id}",
        })
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "work_package": WORK_PACKAGE,
        "platform": "macos-arm64",
        "git_commit": "0" * 40,
        "command": "python3 scripts/run_wp_607a_native_gui_gate.py",
        "os_build": "Synthetic OS 1.0",
        "architecture": "arm64",
        "compiler": "clang 15",
        "qt_version": "6.8.0",
        "qt_platform_plugin": "cocoa",
        "display_session": "aqua",
        "logical_dpi": "96.00",
        "device_pixel_ratio": "2.00",
        "cpu": "arm64",
        "memory": "16 GiB",
        "machine_label": "wp607a-self-test-desktop",
        "corpus_revision": CORPUS_REVISION,
        "fixtures": fixtures,
        "utc_timestamp": "2026-09-04T00:00:00Z",
        "status": "PASS",
        "out_of_scope": list(OUT_OF_SCOPE_IDS),
        "cells": cells,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=tuple(NATIVE_PLUGINS),
                        help="native platform to gate")
    parser.add_argument("--preset", default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--manual-template", dest="manual_template",
                        choices=tuple(NATIVE_PLUGINS),
                        help="write the manual checklist template")
    parser.add_argument("--self-test", action="store_true",
                        help="verify the schema/refusal contract and the "
                             "command plans without capturing")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.self_test:
        return run_self_test()
    if args.manual_template:
        try:
            return write_manual_template(args.manual_template)
        except Refused as refusal:
            print(f"WP-607A runner: REFUSED — {refusal}", file=sys.stderr)
            return 2
    if not args.platform:
        parser.error("--platform is required unless --self-test is used")
    try:
        return run_gate(args.platform, args.preset, args.jobs)
    except Refused as refusal:
        print(f"WP-607A runner: REFUSED — {refusal}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
