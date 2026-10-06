"""Package the complete Windows Release runtime without debug symbols or local settings."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
VERSION = (ROOT / "POLDEN_VERSION").read_text(encoding="utf-8").strip()


def selected_local_config(source: Path) -> set[str]:
    config = source / "config" / "obs-studio"
    values = {}
    section = ""
    for line in (config / "user.ini").read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "Basic" and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    profile = values["ProfileDir"]
    scene = values["SceneCollectionFile"]
    prefix = "config/obs-studio"
    return {
        f"{prefix}/user.ini",
        f"{prefix}/basic/profiles/{profile}/basic.ini",
        f"{prefix}/basic/profiles/{profile}/recordEncoder.json",
        f"{prefix}/basic/scenes/{scene}",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--include-local-settings",
        action="store_true",
        help="Include the imported personal OBS profile, scenes, and InfoWriter plugin",
    )
    parser.add_argument("--build-dir", default="build_polden_next", help="Build directory containing rundir/Release")
    parser.add_argument(
        "--personal-source",
        default="latest",
        help="Existing portable runtime containing the OBS profile and InfoWriter",
    )
    args = parser.parse_args()
    source = ROOT / args.build_dir / "rundir" / "Release"
    personal = ROOT / args.personal_source
    if not (source / "bin" / "64bit" / "obs64.exe").is_file():
        raise SystemExit("Release build is missing; build Polden OBS first")

    suffix = "-Personal" if args.include_local_settings else ""
    output = DIST / f"Polden-OBS-{VERSION}-Windows-x64{suffix}.zip"
    local_config = selected_local_config(personal) if args.include_local_settings else set()
    output.parent.mkdir(exist_ok=True)
    files = {}
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            if path.suffix.lower() == ".pdb":
                continue
            if relative.parts[0] == "config":
                continue
            if relative.parts[0] not in {"bin", "data", "obs-plugins"} and relative.as_posix() not in {
                "portable_mode.txt", "COPYING"
            }:
                continue
            if relative.as_posix() == "obs-plugins/64bit/OBSInfoWriter.dll":
                continue
            if relative.as_posix() == "COPYING":
                continue
            archive.write(path, Path("Polden OBS") / relative)
            files[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        if "portable_mode.txt" not in files:
            archive.writestr("Polden OBS/portable_mode.txt", b"")
            files["portable_mode.txt"] = hashlib.sha256(b"").hexdigest()
        license_bytes = (ROOT / "COPYING").read_bytes()
        archive.writestr("Polden OBS/COPYING", license_bytes)
        files["COPYING"] = hashlib.sha256(license_bytes).hexdigest()
        manifest = {"schema": 1, "version": VERSION, "files": files}
        archive.writestr("Polden OBS/polden-install.json", json.dumps(manifest, indent=2) + "\n")
        if args.include_local_settings:
            for relative in sorted(local_config | {"obs-plugins/64bit/OBSInfoWriter.dll"}):
                path = personal / relative
                if path.is_file():
                    archive.write(path, Path("Polden OBS") / relative)

    print(f"Created {output} ({output.stat().st_size / 1024**2:.1f} MiB)")
    with output.open("rb") as archive_file:
        checksum = hashlib.file_digest(archive_file, "sha256").hexdigest()
    output.with_suffix(".zip.sha256").write_text(f"{checksum}  {output.name}\n", encoding="utf-8")


if __name__ == "__main__":
    main()
