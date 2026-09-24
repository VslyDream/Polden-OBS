"""Build a Premiere UXP .ccx archive from the bridge panel."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "premiere" / "polden-bridge"
OUTPUT = ROOT / "dist" / "Polden-OBS-Bridge-premierepro.ccx"


def main() -> None:
    OUTPUT.parent.mkdir(exist_ok=True)
    with ZipFile(OUTPUT, "w", compression=ZIP_DEFLATED) as archive:
        for name in ("manifest.json", "index.html", "index.js"):
            archive.write(PLUGIN / name, name)
    print(f"Created {OUTPUT}")


if __name__ == "__main__":
    main()
