"""Brand, ad-hoc sign and verify a macOS build from the unchanged 0.1.0 release tag."""

import hashlib
import os
from pathlib import Path
import plistlib
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    source = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = Path("POLDEN_VERSION").read_text(encoding="utf-8").strip()
    assert version == "0.1.0"
    arch = os.environ["POLDEN_ARCH"]
    assert arch in ("arm64", "x86_64")
    app = source.with_name("Polden OBS.app")
    source.rename(app)
    info_path = app / "Contents/Info.plist"
    with info_path.open("rb") as file:
        info = plistlib.load(file)
    info.update({
        "CFBundleDisplayName": "Polden OBS", "CFBundleName": "Polden OBS",
        "CFBundleIdentifier": "com.vslydream.polden-obs",
        "CFBundleShortVersionString": version, "CFBundleVersion": version,
        "CFBundleIconFile": "Polden.icns",
    })
    info.pop("CFBundleIconName", None)
    with info_path.open("wb") as file:
        plistlib.dump(info, file)
    iconset = output.parent / "Polden.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
            run("sips", "-z", size * scale, size * scale, "frontend/forms/images/polden.png", "--out", iconset / name,
                stdout=subprocess.DEVNULL)
    run("iconutil", "-c", "icns", iconset, "-o", app / "Contents/Resources/Polden.icns")
    run("codesign", "--force", "--deep", "--sign", "-", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    executable = app / "Contents/MacOS" / info["CFBundleExecutable"]
    run("lipo", "-verify_arch", arch, executable)
    result = run(executable, "--version", capture_output=True, text=True)
    print(result.stdout.strip())
    assert "Polden OBS - 0.1.0 (OBS 32.2.2)" in result.stdout
    # Verify that all embedded dynamic libraries use relocatable paths.
    for file in app.rglob("*"):
        if file.is_file() and (file.suffix in (".dylib", ".so") or file == executable):
            dependencies = run("otool", "-L", file, capture_output=True, text=True).stdout.splitlines()[1:]
            for line in dependencies:
                dependency = line.strip().split(" (", 1)[0]
                assert not dependency.startswith(("/Users/", "/opt/homebrew/", "/usr/local/")), (file, dependency)
    archive = output / f"Polden-OBS-{version}-macOS-{arch}.zip"
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive)
    with archive.open("rb") as file:
        checksum = hashlib.file_digest(file, "sha256").hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{checksum}  {archive.name}\n", encoding="utf-8")
    print(f"Packaged {archive.name}: {archive.stat().st_size} bytes")


if __name__ == "__main__":
    main()
