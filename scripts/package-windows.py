"""Package the complete Windows Release runtime without debug symbols or local settings."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build_x64" / "rundir" / "Release"
OUTPUT = ROOT / "dist" / "Polden-OBS-32.2.2-Windows-x64.zip"


def main() -> None:
    if not (SOURCE / "bin" / "64bit" / "obs64.exe").is_file():
        raise SystemExit("Release build is missing; build Polden OBS first")

    OUTPUT.parent.mkdir(exist_ok=True)
    with ZipFile(OUTPUT, "w", compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(SOURCE.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(SOURCE)
            if relative.parts[0] == "config" or path.suffix.lower() == ".pdb":
                continue
            archive.write(path, Path("Polden OBS") / relative)

    print(f"Created {OUTPUT} ({OUTPUT.stat().st_size / 1024**2:.1f} MiB)")


if __name__ == "__main__":
    main()
