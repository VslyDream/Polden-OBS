"""Exercise local publication using disposable runtimes and a stub Start Menu installer."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    environment = {key: value for key, value in os.environ.items() if key.casefold() != "path"}
    environment["Path"] = os.environ.get("Path", os.environ.get("PATH", ""))
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="polden-publish-", dir=ROOT / "tmp") as temporary:
        work = Path(temporary)
        assert work.resolve().is_relative_to((ROOT / "tmp").resolve())
        scripts = work / "scripts"
        scripts.mkdir()
        shutil.copy2(ROOT / "scripts/Publish-PoldenBuild.ps1", scripts)
        (scripts / "Install-PoldenShortcut.ps1").write_text(
            "param([string] $Executable)\n[IO.File]::WriteAllText((Join-Path $PSScriptRoot '../shortcut-target.txt'), $Executable)\n",
            encoding="utf-8",
        )
        old = work / "old/rundir/Release"
        new = work / "new/rundir/Release"

        def write(runtime, relative, content):
            target = runtime / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)

        write(old, "bin/64bit/obs64.exe", b"old build")
        write(old, "portable_mode.txt", b"")
        write(old, "config/obs-studio/user.ini", b"personal profile")
        write(old, "config/obs-studio/polden.json", b'{"projects":["my-game"]}')
        write(old, "config/obs-studio/hidden-settings.json", b"hidden settings")
        write(old, "obs-plugins/64bit/custom.dll", b"custom plugin")
        write(old, "obs-plugins/64bit/bundled.dll", b"old bundled plugin")
        write(old, "data/obs-plugins/custom/data.txt", b"custom plugin data")
        write(new, "bin/64bit/obs64.exe", b"new build")
        write(new, "config/obs-studio/user.ini", b"stale test profile")
        write(new, "config/obs-studio/stale-only.json", b"stale test settings")
        write(new, "obs-plugins/64bit/bundled.dll", b"new bundled plugin")
        subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        "New-Item -ItemType Junction -Path $env:POLDEN_TEST_LATEST -Target $env:POLDEN_TEST_OLD | Out-Null; "
                        "(Get-Item -LiteralPath (Join-Path $env:POLDEN_TEST_OLD 'config/obs-studio/hidden-settings.json')).Attributes = 'Hidden'"],
                       env={**environment, "POLDEN_TEST_LATEST": str(work / "latest"), "POLDEN_TEST_OLD": str(old)}, check=True)

        def publish(running):
            command = (
                "function Get-CimInstance { param($ClassName, $Filter, $ErrorAction) "
                + ("[pscustomobject]@{ExecutablePath=$env:POLDEN_TEST_RUNNING;ProcessId=123}" if running else "")
                + " }; & $env:POLDEN_TEST_SCRIPT -BuildDir new"
            )
            return subprocess.run(["powershell.exe", "-NoProfile", "-Command", command],
                                  env={**environment, "POLDEN_TEST_RUNNING": str(old / "bin/64bit/obs64.exe"),
                                       "POLDEN_TEST_SCRIPT": str(scripts / "Publish-PoldenBuild.ps1")},
                                  capture_output=True, text=True)

        blocked = publish(True)
        assert blocked.returncode != 0 and "running" in blocked.stderr, blocked.stderr
        assert (work / "latest").resolve() == old
        assert (new / "config/obs-studio/user.ini").read_bytes() == b"stale test profile"
        assert not list((work / "local-build-backups").glob("*"))
        installed = publish(False)
        assert installed.returncode == 0, installed.stderr
        assert (work / "latest").resolve() == new
        assert (new / "config/obs-studio/user.ini").read_bytes() == b"personal profile"
        assert (new / "config/obs-studio/polden.json").read_bytes() == b'{"projects":["my-game"]}'
        assert (new / "config/obs-studio/hidden-settings.json").read_bytes() == b"hidden settings"
        assert not (new / "config/obs-studio/stale-only.json").exists()
        assert (new / "portable_mode.txt").exists()
        assert (new / "obs-plugins/64bit/custom.dll").read_bytes() == b"custom plugin"
        assert (new / "data/obs-plugins/custom/data.txt").read_bytes() == b"custom plugin data"
        assert (new / "obs-plugins/64bit/bundled.dll").read_bytes() == b"new bundled plugin"
        backups = list((work / "local-build-backups").glob("*/previous-build-config/obs-studio/user.ini"))
        assert len(backups) == 1 and backups[0].read_bytes() == b"stale test profile"
        assert (old / "config/obs-studio/user.ini").read_bytes() == b"personal profile"
        assert Path((work / "shortcut-target.txt").read_text()).resolve() == new / "bin/64bit/obs64.exe"
        assert publish(False).returncode == 0
        # Remove the junction itself before TemporaryDirectory's recursive cleanup.
        (work / "latest").rmdir()

    spec = importlib.util.spec_from_file_location("polden_build", ROOT / "scripts/build-windows.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with patch.object(module.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "[123]", "")):
        assert not module.check_build_not_running("build_polden_next", environment)
    with patch.object(module.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "[]", "")):
        assert module.check_build_not_running("build_polden_next", environment)
    print("PASS: running OBS blocks publication before mutations; settings, plugins, portable mode, backups, shortcut and pre-build guard")


if __name__ == "__main__":
    main()
