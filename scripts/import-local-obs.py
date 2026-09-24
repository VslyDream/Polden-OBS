"""Import the active OBS recording profile and InfoWriter into the local build.

Only the portable build is modified. The source OBS profile is read only, and
personal settings and plugin binaries remain outside Git.
"""

import argparse
import hashlib
import os
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build_x64" / "rundir" / "Release"
SOURCE_CONFIG = Path(os.environ["APPDATA"]) / "obs-studio"


def basic_values(path: Path) -> dict[str, str]:
    values = {}
    section = ""
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"\s*\[([^]]+)\]\s*", line)
        if match:
            section = match.group(1)
        elif section == "Basic" and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def set_basic_values(path: Path, changes: dict[str, str]) -> None:
    lines = path.read_text(encoding="utf-8-sig").splitlines(keepends=True)
    section = ""
    seen = set()
    output = []
    for line in lines:
        match = re.fullmatch(r"\s*\[([^]]+)\]\s*", line.strip())
        if match:
            if section == "Basic":
                output.extend(f"{key}={value}\n" for key, value in changes.items() if key not in seen)
            section = match.group(1)
        if section == "Basic" and "=" in line and not line.lstrip().startswith(";"):
            key = line.split("=", 1)[0].strip()
            if key in changes:
                output.append(f"{key}={changes[key]}\n")
                seen.add(key)
                continue
        output.append(line)
    if section == "Basic":
        output.extend(f"{key}={value}\n" for key, value in changes.items() if key not in seen)
    path.write_text("".join(output), encoding="utf-8")


def copy_with_backup(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and hashlib.sha256(destination.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
        backup = destination.with_name(destination.name + ".before-obs-import")
        if not backup.exists():
            shutil.copy2(destination, backup)
    shutil.copy2(source, destination)
    if hashlib.sha256(destination.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
        raise RuntimeError(f"Copy verification failed: {destination}")
    print(f"Copied {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, required=True, help="Current OBS installation directory")
    args = parser.parse_args()
    target_config = RUNTIME / "config" / "obs-studio"
    target_user = target_config / "user.ini"
    if not target_user.is_file():
        raise SystemExit("Portable Polden OBS has not been started yet")

    current = basic_values(SOURCE_CONFIG / "user.ini")
    required = ("Profile", "ProfileDir", "SceneCollection", "SceneCollectionFile")
    if any(not current.get(key) for key in required):
        raise SystemExit("Could not identify the active OBS profile and scene collection")

    source_profile = SOURCE_CONFIG / "basic" / "profiles" / current["ProfileDir"]
    target_profile = target_config / "basic" / "profiles" / current["ProfileDir"]
    copy_with_backup(source_profile / "basic.ini", target_profile / "basic.ini")
    record_encoder = source_profile / "recordEncoder.json"
    if record_encoder.is_file():
        copy_with_backup(record_encoder, target_profile / "recordEncoder.json")

    scene_file = current["SceneCollectionFile"]
    copy_with_backup(
        SOURCE_CONFIG / "basic" / "scenes" / scene_file,
        target_config / "basic" / "scenes" / scene_file,
    )
    copy_with_backup(
        args.install_root / "obs-plugins" / "64bit" / "OBSInfoWriter.dll",
        RUNTIME / "obs-plugins" / "64bit" / "OBSInfoWriter.dll",
    )

    backup_user = target_user.with_name("user.ini.before-obs-import")
    if not backup_user.exists():
        shutil.copy2(target_user, backup_user)
    set_basic_values(target_user, {key: current[key] for key in required})
    print(f"Selected OBS profile {current['Profile']!r} and scene collection {current['SceneCollection']!r}")


if __name__ == "__main__":
    main()
