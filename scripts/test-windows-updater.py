"""Exercise the real Windows helper on disposable portable installations."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
from contextlib import nullcontext
import uuid
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "Update-Polden.ps1"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def snapshot(folder):
    return {
        p.relative_to(folder).as_posix(): p.read_bytes()
        for p in folder.rglob("*")
        if p.is_file() and ".polden-backups" not in p.relative_to(folder).parts
    }


def exercise(case):
    test_root = ROOT / "build_polden_next" / "update-tests"
    test_root.mkdir(exist_ok=True)
    with nullcontext(test_root / ("case-" + uuid.uuid4().hex)) as temporary:
        work = Path(temporary)
        work.mkdir()
        install = work / "portable"
        request_dir = work / "request"
        request_dir.mkdir()
        old_files = {
            "bin/64bit/obs64.exe": b"old executable",
            "data/old.txt": b"obsolete bundled resource",
        }
        new_files = {
            "bin/64bit/obs64.exe": b"new executable",
            "data/new.txt": b"new bundled resource",
        }
        user_files = {
            "config/obs-studio/user.ini": b"user settings",
            "config/obs-studio/polden.json": b"user projects",
            "config/obs-studio/basic/scenes/test.json": b"user scenes",
            "obs-plugins/64bit/custom.dll": b"custom plugin",
            "data/obs-plugins/custom/settings.json": b"plugin settings",
        }
        if case == "custom_collision":
            new_files["obs-plugins/64bit/custom.dll"] = b"must not overwrite user plugin"
        if case == "rollback":
            user_files["data/blocker"] = b"a user file blocking a new directory"
            new_files["data/blocker/new.txt"] = b"triggers a failure after executable replacement"
        old_manifest = {"schema": 1, "version": "0.1.0", "files": {k: digest(v) for k, v in old_files.items()}}
        for relative, content in (old_files | user_files | {"polden-install.json": json.dumps(old_manifest).encode()}).items():
            target = install / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        if case == "link_escape":
            external = work / "external"
            external.mkdir()
            environment = os.environ.copy()
            environment["POLDEN_TEST_LINK"] = str(install / "data" / "linked")
            environment["POLDEN_TEST_TARGET"] = str(external)
            subprocess.run([
                "powershell.exe", "-NoProfile", "-Command",
                "New-Item -ItemType Junction -Path $env:POLDEN_TEST_LINK -Target $env:POLDEN_TEST_TARGET | Out-Null",
            ], check=True, capture_output=True, env=environment)
            new_files["data/linked/file.txt"] = b"must never follow a junction"
        before = snapshot(install)
        new_manifest = {"schema": 1, "version": "0.1.1", "files": {k: digest(v) for k, v in new_files.items()}}
        if case == "bad_file_hash":
            new_manifest["files"]["bin/64bit/obs64.exe"] = "0" * 64
        archive_path = request_dir / "update.zip"
        with ZipFile(archive_path, "w") as archive:
            for relative, content in new_files.items():
                archive.writestr("Polden OBS/" + relative, content)
            archive.writestr("Polden OBS/polden-install.json", json.dumps(new_manifest))
            if case == "traversal":
                archive.writestr("Polden OBS/../../escaped.txt", b"must never escape")
            if case == "settings_in_archive":
                archive.writestr("Polden OBS/config/obs-studio/user.ini", b"must not overwrite")
        request = {
            "root": str(install), "pid": 2147483647, "version": "0.1.1",
            "sha256": digest(archive_path.read_bytes()),
            "settings": str(install / "config" / "obs-studio"), "portable": True,
        }
        if case == "bad_archive_hash":
            request["sha256"] = "0" * 64
        if case == "downgrade":
            request["version"] = "0.0.1"
        request_path = request_dir / "request.json"
        request_path.write_text(json.dumps(request), encoding="utf-8")
        result = subprocess.run([
            "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HELPER),
            "-RequestPath", str(request_path), "-NoRestart", "-Quiet",
        ], capture_output=True, text=True, timeout=45)
        log = (request_dir / "update.log").read_text(encoding="utf-8-sig")
        if case == "success":
            assert result.returncode == 0, (result.stderr, log)
            assert (install / "bin/64bit/obs64.exe").read_bytes() == new_files["bin/64bit/obs64.exe"]
            assert not (install / "data/old.txt").exists()
            assert (install / "data/new.txt").read_bytes() == new_files["data/new.txt"]
            for relative, content in user_files.items():
                assert (install / relative).read_bytes() == content, relative
            backups = list((install / ".polden-backups").iterdir())
            assert len(backups) == 1
            assert (backups[0] / "settings/user.ini").read_bytes() == b"user settings"
            assert (backups[0] / "files/bin/64bit/obs64.exe").read_bytes() == b"old executable"
        else:
            assert result.returncode != 0, log
            assert snapshot(install) == before, log
            assert not (work / "escaped.txt").exists()
            if case == "rollback":
                assert "Previous version restored" in log, log
            if case == "link_escape":
                assert not list(external.iterdir()), log
                # rmdir removes the junction, preserving its external target.
                os.rmdir(install / "data" / "linked")
        print(f"PASS {case}")


if __name__ == "__main__":
    for name in ("success", "bad_archive_hash", "bad_file_hash", "traversal", "settings_in_archive", "custom_collision", "downgrade", "rollback", "link_escape"):
        exercise(name)
