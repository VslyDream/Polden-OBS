"""Build the configured OBS solution with a Windows-safe environment."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
VSWHERE = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Release")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    install_path = subprocess.check_output(
        [str(VSWHERE), "-latest", "-products", "*", "-property", "installationPath"], text=True
    ).strip()
    msbuild = Path(install_path) / "MSBuild" / "Current" / "Bin" / "MSBuild.exe"
    if not msbuild.is_file():
        raise FileNotFoundError(msbuild)

    # Some launchers provide both Path and PATH. MSBuild's .NET Framework tool
    # launcher throws MSB6001 when it copies that environment to child processes.
    environment = {key: value for key, value in os.environ.items() if key.casefold() != "path"}
    environment["Path"] = os.environ.get("Path", os.environ.get("PATH", ""))
    command = [
        str(msbuild),
        str(ROOT / "build_x64" / "frontend" / "obs-studio.vcxproj"),
        f"/p:Configuration={args.config}",
        "/p:Platform=x64",
        f"/m:{args.jobs}",
        "/nr:false",
        "/nologo",
        "/verbosity:minimal",
    ]
    return subprocess.call(command, cwd=ROOT, env=environment)


if __name__ == "__main__":
    sys.exit(main())
