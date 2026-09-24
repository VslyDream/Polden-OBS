"""Package the complete Windows Release runtime without debug symbols or local settings."""

import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build_x64" / "rundir" / "Release"
DIST = ROOT / "dist"


def selected_local_config() -> set[str]:
    config = SOURCE / "config" / "obs-studio"
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
    args = parser.parse_args()
    if not (SOURCE / "bin" / "64bit" / "obs64.exe").is_file():
        raise SystemExit("Release build is missing; build Polden OBS first")

    suffix = "-Personal" if args.include_local_settings else ""
    output = DIST / f"Polden-OBS-32.2.2-Windows-x64{suffix}.zip"
    local_config = selected_local_config() if args.include_local_settings else set()
    output.parent.mkdir(exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(SOURCE.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(SOURCE)
            if path.suffix.lower() == ".pdb":
                continue
            if relative.parts[0] == "config" and relative.as_posix() not in local_config:
                continue
            if not args.include_local_settings and relative.as_posix() == "obs-plugins/64bit/OBSInfoWriter.dll":
                continue
            archive.write(path, Path("Polden OBS") / relative)

    print(f"Created {output} ({output.stat().st_size / 1024**2:.1f} MiB)")


if __name__ == "__main__":
    main()
