"""Source fingerprints shared by isolated builds and artifact verification."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

CONTRACT_VERSION = 4


def source_files(plugin: Path) -> list[Path]:
    files = [plugin / f"{plugin.name}.uplugin"]
    for directory in ("Source", "Config"):
        files.extend(path for path in (plugin / directory).rglob("*") if path.is_file())
    for path in files:
        if path.is_symlink() or not path.resolve().is_relative_to(plugin.resolve()):
            raise ValueError(f"linked build input: {path}")
    return sorted(files, key=lambda path: path.relative_to(plugin).as_posix())


def fingerprint(plugin: Path) -> str:
    digest = hashlib.sha256()
    for file in source_files(plugin):
        digest.update(file.relative_to(plugin).as_posix().encode("utf-8") + b"\0")
        digest.update(file.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def write_identity(plugin: Path, repository: Path) -> dict:
    descriptor = json.loads((plugin / f"{plugin.name}.uplugin").read_text(encoding="utf-8"))
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=repository, text=True).strip())
    data = dict(plugin=plugin.name, version=descriptor["VersionName"], source_commit=commit,
                source_dirty=dirty, source_fingerprint=fingerprint(plugin), contract_version=CONTRACT_VERSION)
    text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    path = plugin / "BuildIdentity.json"
    if not path.is_file() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")
    return data
