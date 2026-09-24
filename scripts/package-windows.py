"""Package the complete Windows Release runtime without debug symbols or local settings."""

import argparse
from io import BytesIO
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"


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
    parser.add_argument("--build-dir", default="build_x64", help="Build directory containing rundir/Release")
    parser.add_argument(
        "--personal-source",
        default="build_x64/rundir/Release",
        help="Existing portable runtime containing the OBS profile and InfoWriter",
    )
    args = parser.parse_args()
    source = ROOT / args.build_dir / "rundir" / "Release"
    personal = ROOT / args.personal_source
    if not (source / "bin" / "64bit" / "obs64.exe").is_file():
        raise SystemExit("Release build is missing; build Polden OBS first")

    suffix = "-Personal" if args.include_local_settings else ""
    output = DIST / f"Polden-OBS-32.2.2-Windows-x64{suffix}.zip"
    local_config = selected_local_config(personal) if args.include_local_settings else set()
    output.parent.mkdir(exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            if path.suffix.lower() == ".pdb":
                continue
            if relative.parts[0] == "config":
                continue
            if relative.as_posix() == "obs-plugins/64bit/OBSInfoWriter.dll":
                continue
            archive.write(path, Path("Polden OBS") / relative)
        for path in sorted((ROOT / "premiere" / "polden-bridge").iterdir()):
            if path.is_file():
                archive.write(path, Path("Polden OBS") / "premiere" / "polden-bridge" / path.name)
        premiere_package = BytesIO()
        with ZipFile(premiere_package, "w", compression=ZIP_DEFLATED) as bridge_archive:
            for name in ("manifest.json", "index.html", "index.js"):
                bridge_archive.write(ROOT / "premiere" / "polden-bridge" / name, name)
        archive.writestr(
            (Path("Polden OBS") / "premiere" / "Polden-OBS-Bridge-premierepro.ccx").as_posix(),
            premiere_package.getvalue(),
        )
        if args.include_local_settings:
            for relative in sorted(local_config | {"obs-plugins/64bit/OBSInfoWriter.dll"}):
                path = personal / relative
                if path.is_file():
                    archive.write(path, Path("Polden OBS") / relative)

    print(f"Created {output} ({output.stat().st_size / 1024**2:.1f} MiB)")


if __name__ == "__main__":
    main()
