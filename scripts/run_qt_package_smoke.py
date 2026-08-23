#!/usr/bin/env python3
"""Build, deploy and launch the optional Qt GUI native package (WP-605F).

The gate is intentionally configured only on a host with both a Qt kit and
the platform deployment tool. Missing tooling is reported as NOT_CONFIGURED;
it must never be mistaken for a passing GUI package result.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build" / "release"
PACKAGE_DIR = ROOT / "build" / "qt-packages"
PREFIX = "png-analyzer-"


def run(command, environment, *, capture=False):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=True,
        text=True,
        capture_output=capture,
    )


def not_configured(reason):
    print(f"qt package smoke: NOT_CONFIGURED ({reason})")
    return 0


def launch_bounded(binary, environment):
    launch_environment = environment.copy()
    launch_environment["QT_QPA_PLATFORM"] = "offscreen"
    print(f"$ {binary} -platform offscreen", flush=True)
    process = subprocess.Popen(
        [str(binary), "-platform", "offscreen"],
        cwd=ROOT,
        env=launch_environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        return_code = process.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.0)
        return_code = 0
    if return_code != 0:
        stdout, stderr = process.communicate()
        raise SystemExit(
            f"GUI exited before bounded smoke completed ({return_code}); "
            f"stdout={stdout!r} stderr={stderr!r}"
        )


def main():
    if sys.platform == "darwin":
        generator = "DragNDrop"
        suffix = ".dmg"
        deploy_tool = shutil.which("macdeployqt")
        required_tools = ("cmake", "cpack", "hdiutil")
    elif os.name == "nt":
        generator = "NSIS"
        suffix = ".exe"
        deploy_tool = shutil.which("windeployqt")
        required_tools = ("cmake", "cpack")
    else:
        return not_configured("macOS or Windows host required")

    if deploy_tool is None:
        return not_configured("platform Qt deployment tool is unavailable")
    for tool in required_tools:
        if shutil.which(tool) is None:
            return not_configured(f"required tool is unavailable: {tool}")
    if shutil.which("qtpaths") is None and shutil.which("qtpaths6") is None:
        return not_configured("required tool is unavailable: qtpaths/qtpaths6")

    environment = os.environ.copy()
    configure = [
        "cmake",
        "--preset",
        "release",
        "-DPNGA_ENABLE_QT_DEPLOYMENT=ON",
    ]
    try:
        run(configure, environment)
    except subprocess.CalledProcessError:
        if not (BUILD_DIR / "CMakeCache.txt").is_file():
            return not_configured("Qt-enabled release configure is unavailable")
        raise

    target_probe = subprocess.run(
        ["cmake", "--build", "--preset", "release", "--target", "help"],
        cwd=ROOT,
        env=environment,
        check=False,
        text=True,
        capture_output=True,
    )
    if "pnga_analyzer_gui" not in target_probe.stdout:
        return not_configured("Qt GUI target is not present in the release build")

    run(
        [
            "cmake",
            "--build",
            "--preset",
            "release",
            "--target",
            "pnga_analyzer_gui",
            "--parallel",
            "4",
        ],
        environment,
    )

    gui_target = BUILD_DIR / "apps" / "png-analyzer-gui"
    if sys.platform == "darwin":
        gui_binary = gui_target / "pnga_analyzer_gui.app" / "Contents" / "MacOS" / "pnga_analyzer_gui"
    else:
        gui_binary = gui_target / "pnga_analyzer_gui.exe"
    if not gui_binary.exists():
        raise SystemExit(f"Qt GUI target did not produce an executable: {gui_binary}")

    PACKAGE_DIR.mkdir(parents=True, exist_ok=True)
    for old in PACKAGE_DIR.glob(f"{PREFIX}*"):
        if old.is_file():
            old.unlink()
    run(
        [
            "cpack",
            "--config",
            str(BUILD_DIR / "CPackConfig.cmake"),
            "-G",
            generator,
            "-B",
            str(PACKAGE_DIR),
        ],
        environment,
    )
    packages = sorted(PACKAGE_DIR.glob(f"{PREFIX}*{suffix}"))
    if len(packages) != 1:
        raise SystemExit(f"expected one {generator} package, found: {packages}")
    package = packages[0]

    if sys.platform == "darwin":
        with tempfile.TemporaryDirectory(prefix="pnga-qt-dmg-") as temp:
            mount = Path(temp) / "mount"
            mount.mkdir()
            run(
                [
                    "hdiutil",
                    "attach",
                    "-nobrowse",
                    "-readonly",
                    "-mountpoint",
                    str(mount),
                    str(package),
                ],
                environment,
            )
            try:
                binaries = sorted(mount.glob("*.app/Contents/MacOS/pnga_analyzer_gui"))
                if len(binaries) != 1:
                    raise SystemExit(f"expected one mounted GUI, found: {binaries}")
                launch_bounded(binaries[0], environment)
            finally:
                run(["hdiutil", "detach", str(mount)], environment)
    else:
        with tempfile.TemporaryDirectory(prefix="pnga-qt-nsis-") as temp:
            install_root = Path(temp) / "installed"
            run([str(package), "/S", f"/D={install_root}"], environment)
            binaries = sorted(install_root.rglob("pnga_analyzer_gui.exe"))
            if len(binaries) != 1:
                raise SystemExit(f"expected one installed GUI, found: {binaries}")
            launch_bounded(binaries[0], environment)
            uninstaller = install_root / "Uninstall.exe"
            if not uninstaller.is_file():
                raise SystemExit(f"NSIS uninstaller missing: {uninstaller}")
            run([str(uninstaller), "/S"], environment)
            for _ in range(50):
                if not install_root.exists():
                    break
                time.sleep(0.1)
            if install_root.exists():
                raise SystemExit(f"uninstall left GUI install root: {install_root}")

    print(f"qt package smoke: PASS generator={generator} package={package}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
