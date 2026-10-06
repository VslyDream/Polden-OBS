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
    parser.add_argument("--build-dir", default="build_polden_next")
    parser.add_argument("--configure", action="store_true", help="Configure a new Visual Studio build directory first")
    parser.add_argument("--no-publish", action="store_true", help="Keep the current local latest shortcut unchanged")
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
    if args.configure:
        cmake = Path(install_path) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        result = subprocess.call([
            str(cmake), "-S", str(ROOT), "-B", str(ROOT / args.build_dir),
            "-G", "Visual Studio 17 2022", "-A", "x64,version=10.0.26100.0",
            "-DVIRTUALCAM_GUID=A3FCE0F5-3493-419F-958A-ABA1250EC20B", "-DENABLE_BROWSER=ON",
            "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc", "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS",
            f"-DCEF_ROOT_DIR={ROOT / '.deps/cef_binary_6533_windows_x64'}",
        ], cwd=ROOT, env=environment)
        if result != 0:
            return result
    command = [
        str(msbuild),
        str(ROOT / args.build_dir / "frontend" / "obs-studio.vcxproj"),
        f"/p:Configuration={args.config}",
        "/p:Platform=x64",
        f"/m:{args.jobs}",
        "/nr:false",
        "/nologo",
        "/verbosity:minimal",
    ]
    result = subprocess.call(command, cwd=ROOT, env=environment)
    if result != 0 or args.config.casefold() != "release" or args.no_publish:
        return result

    return subprocess.call(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / "scripts" / "Publish-PoldenBuild.ps1"),
            "-BuildDir",
            args.build_dir,
        ],
        cwd=ROOT,
        env=environment,
    )


if __name__ == "__main__":
    sys.exit(main())
