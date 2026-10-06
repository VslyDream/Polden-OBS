"""Publish a verified portable Windows build using the configured Git credential manager."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "VslyDream/Polden-OBS"
API = "https://api.github.com/repos/" + REPOSITORY


def credential():
    environment = os.environ.copy()
    environment["GIT_TERMINAL_PROMPT"] = "0"
    environment["GCM_INTERACTIVE"] = "Never"
    result = subprocess.run(
        ["git", "-c", "credential.helper=manager", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n", text=True,
        capture_output=True, env=environment, check=False,
    )
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    token = values.get("password")
    if not token:
        raise RuntimeError("GitHub credentials are unavailable in the configured credential manager")
    return token


def api(token, url, method="GET", data=None, content_type="application/json"):
    request = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
        "User-Agent": "Polden-release", "Content-Type": content_type,
    })
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        detail = json.loads(error.read()).get("message", "GitHub request failed")
        raise RuntimeError(f"GitHub HTTP {error.code}: {detail}") from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    token = credential()
    repo = api(token, API)
    print(f"Repository: {repo['full_name']}; public: {not repo['private']}; push access: {repo.get('permissions', {}).get('push')}")
    releases = api(token, API + "/releases?per_page=100")
    print("Existing releases:", ", ".join(item["tag_name"] for item in releases) or "none")
    if args.check or not args.publish:
        return
    version = (ROOT / "POLDEN_VERSION").read_text(encoding="utf-8").strip()
    tag = "polden-v" + version
    archive = ROOT / "dist" / f"Polden-OBS-{version}-Windows-x64.zip"
    with archive.open("rb") as file:
        checksum = hashlib.file_digest(file, "sha256").hexdigest()
    with ZipFile(archive) as file:
        manifest = json.loads(file.read("Polden OBS/polden-install.json"))
        if manifest["version"] != version or any(name.startswith("Polden OBS/config/") for name in file.namelist()):
            raise RuntimeError("Archive version or privacy check failed")
        for relative, expected in manifest["files"].items():
            if hashlib.sha256(file.read("Polden OBS/" + relative)).hexdigest() != expected:
                raise RuntimeError("Archive content verification failed: " + relative)
    checksum_file = archive.with_suffix(".zip.sha256")
    if checksum_file.read_text(encoding="utf-8") != f"{checksum}  {archive.name}\n":
        raise RuntimeError("Archive checksum file does not match")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
        raise RuntimeError("Commit the release source before publishing")
    history = (ROOT / "docs/polden/releases.md").read_text(encoding="utf-8")
    section = history.split("## " + version + " — ", 1)[1].split("\n## ", 1)[0]
    changes = section.split("\n", 1)[1].strip()
    body = f"Первый публичный релиз Polden OBS {version} для Windows x64. База: OBS Studio 32.2.2.\n\n{changes}\n\n" + (
        "### Запуск\n\nСкачайте `" + archive.name + "`, распакуйте в папку с правом записи и запустите "
        "`Polden OBS/bin/64bit/obs64.exe`. Архив работает в переносимом режиме; настройки хранятся в `config/`. "
        "Персональные профили, сцены, журналы и InfoWriter в публичный архив не включены.\n\n"
        "Будущие обновления: проверка при запуске, значок рядом с баг-репортом, обновление с резервной копией "
        "настроек и сохранением сторонних плагинов. Во время записи, трансляции и обработки файлов установка блокируется.\n\n"
        "Лицензия: GPL-2.0-or-later. Исходники этой сборки доступны по тегу релиза."
    )
    existing = next((item for item in releases if item["tag_name"] == tag), None)
    if existing and not existing["draft"]:
        raise RuntimeError("This release is already public; refusing to replace it")
    release = existing or api(token, API + "/releases", "POST", json.dumps({
        "tag_name": tag, "target_commitish": commit, "name": "Polden OBS " + version,
        "body": body, "draft": True, "prerelease": False,
    }).encode())
    upload_base = release["upload_url"].split("{", 1)[0]
    assets = api(token, release["assets_url"])
    for file, mime in ((archive, "application/zip"), (checksum_file, "text/plain")):
        uploaded = next((item for item in assets if item["name"] == file.name), None)
        if uploaded:
            if uploaded.get("digest") != "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest():
                raise RuntimeError("A draft asset with this name has different content")
        else:
            uploaded = api(token, upload_base + "?" + urllib.parse.urlencode({"name": file.name}),
                           "POST", file.read_bytes(), mime)
        expected = "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest()
        if uploaded.get("digest") != expected:
            raise RuntimeError("GitHub upload checksum verification failed")
        print("Verified upload:", file.name)
    published = api(token, API + "/releases/" + str(release["id"]), "PATCH", json.dumps({
        "body": body, "draft": False, "make_latest": "true",
    }).encode())
    print("Published:", published["html_url"])


if __name__ == "__main__":
    main()
