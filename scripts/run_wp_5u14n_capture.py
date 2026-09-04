#!/usr/bin/env python3
"""WP-5U14N native theme capture orchestrator.

Builds the test-only capture target, drives the four frozen theme cells for
one platform through per-mode native CTest invocations (never offscreen),
flips the OS appearance between the System cells (macOS: AppleInterfaceStyle
plus an osascript appearance notification; Windows: the AppsUseLightTheme
registry value plus a WM_SETTINGCHANGE broadcast via PowerShell, executed
only when running on Windows), restores the pre-run appearance state even on
failure, validates every capture record against the
pnga-wp5u14n-native-capture-v1 schema and assembles
build/<preset>/evidence/wp-5u14n/evidence.json (sorted keys, no absolute
paths, UTC timestamps).

The runner refuses: offscreen capture attempts (R7), missing corpus
fixtures, dirty capture directories, unknown matrix cells and any record
missing a required field (R2).

Usage:
    python3 scripts/run_wp_5u14n_capture.py --platform macos --preset dev --jobs 4
    python3 scripts/run_wp_5u14n_capture.py --self-test
"""

import argparse
import datetime
import hashlib
import json
import os
import platform as host_platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = "pnga-wp5u14n-native-capture-v1"
WORK_PACKAGE = "WP-5U14N"
CAPTURE_TARGET = "pnga_gui_wp_5u14n_native_capture_tests"

# Frozen matrix (R3): one unit per theme cell, System cells captured in fresh
# processes after an OS appearance flip (R4).
MODE_UNITS = (
    ("system-light", "wp5u14n_capture_system", "light"),
    ("light", "wp5u14n_capture_light", None),
    ("dark", "wp5u14n_capture_dark", None),
    ("system-dark", "wp5u14n_capture_system", "dark"),
)

CELLS = {
    ("macos", "system-light"): "mac-system-light-retina",
    ("macos", "light"): "mac-light-retina",
    ("macos", "dark"): "mac-dark-retina",
    ("macos", "system-dark"): "mac-system-dark-retina",
    ("windows", "system-light"): "win-system-light-100",
    ("windows", "light"): "win-light-100",
    ("windows", "dark"): "win-dark-100",
    ("windows", "system-dark"): "win-system-dark-100",
}

VIEWS = (
    "default",
    "blocks",
    "huffman",
    "decode-trace",
    "narrow-inspector",
    "focus",
    "stored",
)

FIXTURE_IDS = (
    "ui-rgb8-five-filters",
    "ui-gray1-none",
    "trace-stored-literals",
)

REQUIRED_RECORD_FIELDS = (
    "os_build",
    "architecture",
    "qt_version",
    "requested_mode",
    "effective_mode",
    "logical_dpi",
    "device_pixel_ratio",
    "window_size",
    "git_commit",
    "utc_timestamp",
    "result",
)

REQUIRED_CAPTURE_FIELDS = (
    "view",
    "fixture_id",
    "fixture_sha256",
    "capture_png",
    "capture_png_sha256",
)

TIMESTAMP_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
SHA_PATTERN = re.compile(r"^[0-9a-f]{64}$")

HOST_SYSTEM = {"macos": "Darwin", "windows": "Windows"}

MACOS_APPLESCRIPT = ("tell application \"System Events\" to tell appearance "
                     "preferences to set dark mode to {dark}")
WINDOWS_PERSONALIZE_KEY = (r"HKCU\Software\Microsoft\Windows\CurrentVersion"
                           r"\Themes\Personalize")
WINDOWS_BROADCAST_POWERSHELL = (
    "$sig = @'\n"
    '[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]\n'
    "public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg,"
    " UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout,"
    " out UIntPtr lpdwResult);\n"
    "'@; $type = Add-Type -MemberDefinition $sig -Name WmSettingChange"
    " -Namespace Win32 -PassThru; [UIntPtr]$result = [UIntPtr]::Zero;"
    " $null = $type::SendMessageTimeout([IntPtr]0xFFFF, 0x001A,"
    " [UIntPtr]::Zero, 'ImmersiveColorSet', 2, 5000, [ref]$result);"
    " Write-Output 'WM_SETTINGCHANGE broadcast sent'"
)


class Refused(Exception):
    """A frozen precondition failed; the runner must not capture."""


def out_root(preset):
    return f"build/{preset}/evidence/wp-5u14n"


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def absolute_path_strings(value):
    """Every string anywhere in `value` shaped like an absolute path."""
    offenders = []
    if isinstance(value, str):
        if value.startswith("/") or re.match(r"^[A-Za-z]:[\\/]", value):
            offenders.append(value)
    elif isinstance(value, dict):
        for entry in value.values():
            offenders.extend(absolute_path_strings(entry))
    elif isinstance(value, (list, tuple)):
        for entry in value:
            offenders.extend(absolute_path_strings(entry))
    return offenders


def serialize_evidence(record):
    """Compact sorted-key JSON, ASCII escaping, one trailing LF."""
    return json.dumps(record, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":")) + "\n"


def appearance_commands(target, mode):
    """Concrete command list enforcing `mode` (light|dark) on `target`."""
    if target == "macos":
        notify = ["osascript", "-e",
                  MACOS_APPLESCRIPT.format(dark="true" if mode == "dark"
                                           else "false")]
        if mode == "dark":
            return [["defaults", "write", "-g", "AppleInterfaceStyle", "Dark"],
                    notify]
        # macOS marks Light/Auto by the ABSENCE of AppleInterfaceStyle; the
        # delete is tolerated when the key is already absent.
        return [["defaults", "delete", "-g", "AppleInterfaceStyle"], notify]
    if target == "windows":
        value = "1" if mode == "light" else "0"
        return [["reg", "add", WINDOWS_PERSONALIZE_KEY, "/v",
                 "AppsUseLightTheme", "/t", "REG_DWORD", "/d", value, "/f"],
                ["powershell", "-NoProfile", "-Command",
                 WINDOWS_BROADCAST_POWERSHELL]]
    raise ValueError(target)


def plan_commands(target, preset, jobs):
    """The exact, order-pinned capture plan (relative paths only).

    The plan carries the relative output root; the executor resolves it to
    an absolute path for the child environment so records never embed one.
    """
    out = out_root(preset)
    steps = [{
        "kind": "build",
        "command": ["cmake", "--build", "--preset", preset, "--target",
                    CAPTURE_TARGET, "--parallel", str(jobs)],
        "env": None,
    }]
    for unit, entry, os_mode in MODE_UNITS:
        if os_mode is not None:
            steps.append({
                "kind": "appearance",
                "os": target,
                "mode": os_mode,
                "commands": appearance_commands(target, os_mode),
            })
        env = {"PNGA_WP5U14N_OUT": out}
        if os_mode is not None:
            env["PNGA_WP5U14N_OS_MODE"] = os_mode
        steps.append({
            "kind": "ctest",
            "unit": unit,
            "command": ["ctest", "--preset", preset, "-R", entry,
                        "--output-on-failure"],
            "env": env,
        })
    return steps


def run(command, *, capture=False, env=None):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=str(ROOT), check=False, text=True,
                          capture_output=capture, env=env)


# --- OS appearance state (save / ensure / restore) ---------------------------

def macos_read_appearance():
    style = subprocess.run(["defaults", "read", "-g", "AppleInterfaceStyle"],
                           capture_output=True, text=True)
    auto = subprocess.run(
        ["defaults", "read", "-g", "AppleInterfaceStyleSwitchesAutomatically"],
        capture_output=True, text=True)
    return {
        "dark": style.returncode == 0 and style.stdout.strip() == "Dark",
        "auto": None if auto.returncode != 0
                else auto.stdout.strip() == "1",
    }


def macos_ensure_appearance(mode):
    for command in appearance_commands("macos", mode):
        result = run(command, capture=True)
        # macOS reports a missing key as "Domain (...) not found." on stderr.
        tolerant = (command[0] == "defaults" and command[1] == "delete"
                    and ("not found" in (result.stderr or "")
                         or "does not exist" in (result.stderr or "")))
        if result.returncode != 0 and not tolerant:
            sys.stderr.write(result.stderr or "")
            raise SystemExit(
                f"macOS appearance change failed: {' '.join(command)}")


def macos_restore_appearance(saved):
    if saved["dark"]:
        macos_ensure_appearance("dark")
    else:
        macos_ensure_appearance("light")
    # Restore the Auto switch AFTER the osascript call: setting dark mode
    # through System Events would otherwise clear the auto flag.
    if saved["auto"] is None:
        run(["defaults", "delete", "-g",
             "AppleInterfaceStyleSwitchesAutomatically"], capture=True)
    else:
        run(["defaults", "write", "-g",
             "AppleInterfaceStyleSwitchesAutomatically",
             "-bool", "true" if saved["auto"] else "false"], capture=True)


def windows_read_appearance():
    result = subprocess.run(["reg", "query", WINDOWS_PERSONALIZE_KEY, "/v",
                             "AppsUseLightTheme"],
                            capture_output=True, text=True)
    match = re.search(r"AppsUseLightTheme\s+REG_DWORD\s+0x([0-9a-fA-F]+)",
                      result.stdout)
    if result.returncode != 0 or not match:
        raise SystemExit("cannot read AppsUseLightTheme from the registry")
    return "light" if int(match.group(1), 16) == 1 else "dark"


def windows_ensure_appearance(mode):
    for command in appearance_commands("windows", mode):
        result = run(command, capture=True)
        if result.returncode != 0:
            sys.stderr.write(result.stderr or "")
            raise SystemExit(
                f"Windows appearance change failed: {' '.join(command)}")
    readback = windows_read_appearance()
    if readback != mode:
        raise SystemExit(f"Windows appearance flip verification failed: "
                         f"expected {mode}, registry reads {readback}")


def read_appearance(target):
    return macos_read_appearance() if target == "macos" else \
        windows_read_appearance()


def ensure_appearance(target, mode):
    (macos_ensure_appearance if target == "macos"
     else windows_ensure_appearance)(mode)


def restore_appearance(target, saved):
    (macos_restore_appearance if target == "macos"
     else windows_restore_appearance)(saved)


def windows_restore_appearance(saved):
    windows_ensure_appearance(saved["mode"])


def describe_appearance(state):
    if isinstance(state, dict):
        if state["dark"]:
            return "dark"
        return "auto" if state["auto"] else "light"
    return state


# --- frozen refusals ----------------------------------------------------------

def preflight(target):
    if host_platform.system() != HOST_SYSTEM[target]:
        raise Refused(
            f"--platform {target} requires running on "
            f"{HOST_SYSTEM[target]}; native captures cannot be produced "
            "on this host")
    if os.environ.get("QT_QPA_PLATFORM") == "offscreen":
        raise Refused("QT_QPA_PLATFORM=offscreen would fake the native "
                      "capture (R7); unset it and rerun")


def refuse_dirty_capture_dir(out_dir):
    """Refuses any pre-existing native evidence; skip-only records from
    offscreen dev-suite runs prove no native capture happened and are
    cleared so a native run can start."""
    captures = out_dir / "captures"
    records = out_dir / "records"
    stale = []
    skip_records = []
    if (out_dir / "evidence.json").exists():
        stale.append("evidence.json")
    if captures.is_dir():
        stale.extend(f"captures/{path.name}"
                     for path in sorted(captures.iterdir()))
    if records.is_dir():
        for path in sorted(records.iterdir()):
            if path.suffix != ".json":
                stale.append(f"records/{path.name}")
                continue
            try:
                record = json.loads(path.read_text(encoding="utf-8"))
            except ValueError:
                stale.append(f"records/{path.name}")
                continue
            if record.get("result") == "skipped-offscreen":
                skip_records.append(path)
            else:
                stale.append(f"records/{path.name}")
    if stale:
        raise Refused(
            "capture directory is dirty; remove "
            f"{out_root_rel(out_dir)} and rerun (found {', '.join(stale[:5])}"
            f"{'…' if len(stale) > 5 else ''})")
    for path in skip_records:
        path.unlink()


def out_root_rel(out_dir):
    return str(Path(out_dir).relative_to(ROOT)).replace(os.sep, "/")


def corpus_dir(preset):
    return ROOT / "build" / preset / "tests" / "corpus" / "wp-607c"


def load_corpus_registry(preset):
    """Pinned fixture id -> expected sha256 from the generated catalog."""
    catalog_path = corpus_dir(preset) / "index.json"
    if not catalog_path.is_file():
        raise Refused(f"missing corpus catalog {out_root_rel(catalog_path)};"
                      " build the wp607c corpus fixture first")
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
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
                f"missing corpus fixture {fixture_id} "
                f"({out_root_rel(fixture_path)}); build the wp607c corpus "
                "fixture first")
        actual = sha256_file(fixture_path)
        if actual != expected:
            raise Refused(
                f"corpus fixture {fixture_id} bytes do not match the "
                "registry sha256")
        hashes[fixture_id] = actual
    return hashes


# --- record contract (R2) ------------------------------------------------------

def record_violations(record, out_dir, expected_cells, fixture_hashes):
    """All violations of the frozen record contract; empty means valid."""
    violations = []
    if not isinstance(record, dict):
        return ["record is not a JSON object"]
    if record.get("schema") != SCHEMA:
        violations.append("schema")
    cell = record.get("cell")
    if not isinstance(cell, str) or cell not in expected_cells:
        violations.append(f"unknown matrix cell {cell!r}")
        return violations
    for field in REQUIRED_RECORD_FIELDS:
        value = record.get(field)
        if not isinstance(value, str) or not value.strip():
            violations.append(field)
    if record.get("result") == "skipped-offscreen":
        violations.append("offscreen capture attempt (result="
                          "skipped-offscreen)")
    elif record.get("result") != "captured":
        violations.append("result")
    if not TIMESTAMP_PATTERN.match(record.get("utc_timestamp", "")
                                   if isinstance(record.get("utc_timestamp"),
                                                 str) else ""):
        violations.append("utc_timestamp format")
    captures = record.get("captures")
    if not isinstance(captures, list) or len(captures) != len(VIEWS):
        violations.append("captures")
        return violations
    for index, entry in enumerate(captures):
        prefix = f"captures[{index}]."
        if not isinstance(entry, dict):
            violations.append(prefix + "entry")
            continue
        if entry.get("view") != VIEWS[index]:
            violations.append(prefix + "view")
        for field in REQUIRED_CAPTURE_FIELDS:
            value = entry.get(field)
            if not isinstance(value, str) or not value.strip():
                violations.append(prefix + field)
        if not SHA_PATTERN.match(
                entry.get("fixture_sha256", "")
                if isinstance(entry.get("fixture_sha256"), str) else ""):
            violations.append(prefix + "fixture_sha256 hex")
        if not SHA_PATTERN.match(
                entry.get("capture_png_sha256", "")
                if isinstance(entry.get("capture_png_sha256"), str) else ""):
            violations.append(prefix + "capture_png_sha256 hex")
        fixture_id = entry.get("fixture_id")
        if fixture_id not in fixture_hashes:
            violations.append(prefix + f"unknown fixture {fixture_id!r}")
        elif fixture_hashes[fixture_id] != entry.get("fixture_sha256"):
            violations.append(prefix + "fixture_sha256 (corpus registry "
                              "mismatch)")
        expected_png = f"captures/{cell}-{VIEWS[index]}.png"
        if entry.get("capture_png") != expected_png:
            violations.append(prefix + "capture_png")
        else:
            png_path = Path(out_dir) / entry["capture_png"]
            if not png_path.is_file():
                violations.append(prefix + "capture_png (missing file)")
            elif sha256_file(png_path) != entry.get("capture_png_sha256"):
                violations.append(prefix + "capture_png_sha256 (file "
                                  "mismatch)")
    return sorted(set(violations))


def load_records(target, out_dir, expected_cells, fixture_hashes):
    records = []
    for unit, _, _ in MODE_UNITS:
        cell = CELLS[(target, unit)]
        path = out_dir / "records" / f"{cell}.json"
        if not path.is_file():
            raise Refused(f"incomplete capture: missing record for cell "
                          f"{cell}")
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            raise Refused(f"record for cell {cell} is not valid JSON: "
                          f"{error}")
        violations = record_violations(record, out_dir, expected_cells,
                                       fixture_hashes)
        if violations:
            raise Refused(f"record {cell} refused: " + "; ".join(violations))
        records.append(record)
    records.sort(key=lambda record: record["cell"])
    return records


def git_commit():
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                            check=True, capture_output=True, text=True)
    return result.stdout.strip()


def write_evidence(out_dir, record):
    offenders = absolute_path_strings(record)
    if offenders:
        raise SystemExit(
            "WP-5U14N evidence record would contain absolute path(s) "
            f"{offenders[:3]}; refusing to write")
    output = Path(out_dir) / "evidence.json"
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(serialize_evidence(record), encoding="utf-8")
    os.replace(temporary, output)


def run_capture(target, preset, jobs):
    preflight(target)
    out_dir = ROOT / out_root(preset)
    refuse_dirty_capture_dir(out_dir)
    fixture_hashes = load_corpus_registry(preset)
    expected_cells = {CELLS[(target, unit)] for unit, _, _ in MODE_UNITS}
    steps = plan_commands(target, preset, jobs)
    saved_appearance = None
    readback = None
    try:
        for step in steps:
            if step["kind"] == "appearance":
                if saved_appearance is None:
                    saved_appearance = read_appearance(target)
                    print(f"appearance saved: "
                          f"{describe_appearance(saved_appearance)}",
                          flush=True)
                ensure_appearance(target, step["mode"])
                continue
            env = dict(os.environ)
            if step["env"]:
                env.update({name: str(ROOT / value)
                            if name == "PNGA_WP5U14N_OUT" else value
                            for name, value in step["env"].items()})
            # Records must carry the commit the captures belong to, not the
            # configure-time snapshot baked into the binary.
            env.setdefault("PNGA_WP5U14N_COMMIT", git_commit())
            result = run(step["command"], env=env)
            if result.returncode != 0:
                raise SystemExit(
                    f"WP-5U14N capture: FAIL at {step['kind']} step "
                    f"({step.get('unit', 'build')}); no evidence assembled")
    finally:
        if saved_appearance is not None:
            restore_appearance(target, saved_appearance)
            readback = read_appearance(target)
            restored = describe_appearance(readback) == \
                describe_appearance(saved_appearance)
            print(f"appearance restored: "
                  f"{describe_appearance(readback)} "
                  f"({'verified' if restored else 'RESTORE MISMATCH'})",
                  flush=True)
    records = load_records(target, out_dir, expected_cells, fixture_hashes)
    evidence = {
        "schema": SCHEMA,
        "work_package": WORK_PACKAGE,
        "status": "PASS",
        "platform": target,
        "preset": preset,
        "commit": git_commit(),
        "time_utc": utc_now(),
        "views": list(VIEWS),
        "fixtures": fixture_hashes,
        "cells": records,
    }
    if saved_appearance is not None:
        evidence["appearance"] = {
            "saved": describe_appearance(saved_appearance),
            "restored": describe_appearance(readback) ==
                        describe_appearance(saved_appearance),
            "readback": describe_appearance(readback),
        }
    write_evidence(out_dir, evidence)
    print(f"WP-5U14N native capture: PASS ({len(records)} cells x "
          f"{len(VIEWS)} views) evidence={out_root(preset)}/evidence.json")
    return 0


# --- self-test ------------------------------------------------------------------

def synthetic_valid_record(cell, out_dir, fixture_hashes):
    """A complete record plus its seven dummy capture PNG files."""
    captures_dir = Path(out_dir) / "captures"
    captures_dir.mkdir(parents=True, exist_ok=True)
    views = list(VIEWS)
    fixture_for_view = {
        "default": FIXTURE_IDS[0], "blocks": FIXTURE_IDS[0],
        "huffman": FIXTURE_IDS[0], "decode-trace": FIXTURE_IDS[0],
        "narrow-inspector": FIXTURE_IDS[1], "focus": FIXTURE_IDS[0],
        "stored": FIXTURE_IDS[2],
    }
    captures = []
    for view in views:
        png = captures_dir / f"{cell}-{view}.png"
        png.write_bytes(b"\x89PNG\r\n\x1a\n synthetic")
        captures.append({
            "view": view,
            "fixture_id": fixture_for_view[view],
            "fixture_sha256": fixture_hashes[fixture_for_view[view]],
            "capture_png": f"captures/{cell}-{view}.png",
            "capture_png_sha256": sha256_file(png),
        })
    return {
        "schema": SCHEMA,
        "cell": cell,
        "platform": "macos",
        "os_build": "Synthetic OS 1.0",
        "architecture": "test-arch",
        "qt_version": "6.test",
        "requested_mode": "light",
        "effective_mode": "light",
        "logical_dpi": "96.00",
        "device_pixel_ratio": "2.00",
        "window_size": "1200x760",
        "git_commit": "synthetic-commit",
        "utc_timestamp": "2026-09-04T00:00:00Z",
        "result": "captured",
        "captures": captures,
    }


def run_self_test():
    failures = []

    # The macOS plan must equal the pinned sequence exactly.
    expected_macos = [
        {"kind": "build", "command": ["cmake", "--build", "--preset", "dev",
                                      "--target", CAPTURE_TARGET,
                                      "--parallel", "4"], "env": None},
        {"kind": "appearance", "os": "macos", "mode": "light",
         "commands": [["defaults", "delete", "-g", "AppleInterfaceStyle"],
                      ["osascript", "-e", MACOS_APPLESCRIPT.format(
                          dark="false")]]},
        {"kind": "ctest", "unit": "system-light",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_system", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n",
                 "PNGA_WP5U14N_OS_MODE": "light"}},
        {"kind": "ctest", "unit": "light",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_light", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n"}},
        {"kind": "ctest", "unit": "dark",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_dark", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n"}},
        {"kind": "appearance", "os": "macos", "mode": "dark",
         "commands": [["defaults", "write", "-g", "AppleInterfaceStyle",
                       "Dark"],
                      ["osascript", "-e", MACOS_APPLESCRIPT.format(
                          dark="true")]]},
        {"kind": "ctest", "unit": "system-dark",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_system", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n",
                 "PNGA_WP5U14N_OS_MODE": "dark"}},
    ]
    planned = plan_commands("macos", "dev", 4)
    if planned != expected_macos:
        failures.append(f"macos planner drift: {planned}")

    # The Windows plan carries the registry flip and the broadcast, and is
    # only ever executed on a Windows host.
    expected_windows = [
        {"kind": "build", "command": ["cmake", "--build", "--preset", "dev",
                                      "--target", CAPTURE_TARGET,
                                      "--parallel", "4"], "env": None},
        {"kind": "appearance", "os": "windows", "mode": "light",
         "commands": [["reg", "add", WINDOWS_PERSONALIZE_KEY, "/v",
                       "AppsUseLightTheme", "/t", "REG_DWORD", "/d", "1",
                       "/f"],
                      ["powershell", "-NoProfile", "-Command",
                       WINDOWS_BROADCAST_POWERSHELL]]},
        {"kind": "ctest", "unit": "system-light",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_system", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n",
                 "PNGA_WP5U14N_OS_MODE": "light"}},
        {"kind": "ctest", "unit": "light",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_light", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n"}},
        {"kind": "ctest", "unit": "dark",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_dark", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n"}},
        {"kind": "appearance", "os": "windows", "mode": "dark",
         "commands": [["reg", "add", WINDOWS_PERSONALIZE_KEY, "/v",
                       "AppsUseLightTheme", "/t", "REG_DWORD", "/d", "0",
                       "/f"],
                      ["powershell", "-NoProfile", "-Command",
                       WINDOWS_BROADCAST_POWERSHELL]]},
        {"kind": "ctest", "unit": "system-dark",
         "command": ["ctest", "--preset", "dev", "-R",
                     "wp5u14n_capture_system", "--output-on-failure"],
         "env": {"PNGA_WP5U14N_OUT": "build/dev/evidence/wp-5u14n",
                 "PNGA_WP5U14N_OS_MODE": "dark"}},
    ]
    planned = plan_commands("windows", "dev", 4)
    if planned != expected_windows:
        failures.append(f"windows planner drift: {planned}")

    # Record-schema validation: a synthetic complete record passes; removing
    # ANY required field (record or per-capture) must be rejected, as must
    # skip records, unknown cells, registry mismatches and bad timestamps.
    import copy
    import tempfile
    with tempfile.TemporaryDirectory() as temporary:
        out_dir = Path(temporary)
        cell = CELLS[("macos", "light")]
        fixture_hashes = {fixture_id: f"{index:064x}"
                          for index, fixture_id in enumerate(FIXTURE_IDS)}
        record = synthetic_valid_record(cell, out_dir, fixture_hashes)
        violations = record_violations(record, out_dir, {cell},
                                       fixture_hashes)
        if violations:
            failures.append(f"valid synthetic record rejected: {violations}")
        for field in ("schema", "cell") + REQUIRED_RECORD_FIELDS:
            broken = copy.deepcopy(record)
            broken.pop(field, None)
            if not record_violations(broken, out_dir, {cell},
                                     fixture_hashes):
                failures.append(f"missing record field {field} not rejected")
        for field in REQUIRED_CAPTURE_FIELDS:
            broken = copy.deepcopy(record)
            del broken["captures"][3][field]
            if not record_violations(broken, out_dir, {cell},
                                     fixture_hashes):
                failures.append(
                    f"missing capture field {field} not rejected")
        skip = {"schema": SCHEMA, "cell": cell,
                "result": "skipped-offscreen"}
        if not record_violations(skip, out_dir, {cell}, fixture_hashes):
            failures.append("offscreen skip record not rejected")
        unknown = copy.deepcopy(record)
        unknown["cell"] = "win-light-42"
        if not record_violations(unknown, out_dir, {cell}, fixture_hashes):
            failures.append("unknown matrix cell not rejected")
        wrong_fixture = copy.deepcopy(record)
        wrong_fixture["captures"][0]["fixture_sha256"] = "f" * 64
        if not record_violations(wrong_fixture, out_dir, {cell},
                                 fixture_hashes):
            failures.append("corpus registry mismatch not rejected")
        bad_stamp = copy.deepcopy(record)
        bad_stamp["utc_timestamp"] = "2026-09-04 00:00:00"
        if not record_violations(bad_stamp, out_dir, {cell},
                                 fixture_hashes):
            failures.append("malformed utc_timestamp not rejected")
        missing_png = copy.deepcopy(record)
        (out_dir / record["captures"][0]["capture_png"]).unlink()
        if not record_violations(missing_png, out_dir, {cell},
                                 fixture_hashes):
            failures.append("missing capture PNG not rejected")

    # The evidence serialization is deterministic, ASCII-only, LF-terminated,
    # sorted and free of absolute paths.
    sample = {"b": 1, "a": {"z": [1, 2], "y": "x"}}
    first = serialize_evidence(sample)
    second = serialize_evidence(sample)
    if first != second:
        failures.append("evidence serialization is not deterministic")
    if not first.endswith("\n") or first.endswith("\n\n"):
        failures.append("evidence serialization must end with one LF")
    if not first.isascii():
        failures.append("evidence serialization must be ASCII-only")
    if json.loads(first).get("b") != 1 or not first.startswith('{"a"'):
        failures.append("evidence serialization must sort keys")
    if absolute_path_strings(sample):
        failures.append("sanitizer flagged a clean record")
    if not absolute_path_strings({"a": [{"b": "/absolute/path"}]}):
        failures.append("sanitizer missed a nested absolute path")
    if not absolute_path_strings(["C:\\Windows\\path"]):
        failures.append("sanitizer missed a Windows absolute path")

    if failures:
        for failure in failures:
            print(f"self-test FAIL: {failure}")
        print(f"self-test: {len(failures)} failure(s)")
        return 1
    print("self-test: macos and windows command plans match the pinned "
          "capture sequences")
    print("self-test: record validation enforces the R2 schema, the frozen "
          "cells, the corpus registry and the view matrix")
    print("self-test: evidence writer is deterministic, sorted, ASCII, "
          "LF-terminated and path-safe")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("macos", "windows"),
                        help="native platform to capture")
    parser.add_argument("--preset", default="dev")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--self-test", action="store_true",
                        help="verify the command plan and record-schema "
                             "validation without capturing")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.self_test:
        return run_self_test()
    if not args.platform:
        parser.error("--platform is required unless --self-test is used")
    try:
        return run_capture(args.platform, args.preset, args.jobs)
    except Refused as refusal:
        print(f"WP-5U14N capture runner: REFUSED — {refusal}",
              file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
