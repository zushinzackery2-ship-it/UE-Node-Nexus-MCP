"""Prepare a separate editor build containing the native regression sources."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from release.identity import write_identity
from tests.compile_check.prepare_host import prepare as prepare_host


def prepare() -> None:
    host = ROOT / "build/scene-tests"
    prepare_host(host)
    plugin = host / "Plugins/UeNodeNexusBridge"
    source = ROOT / "tests/scene/native"
    target = plugin / "Source/UeNodeNexusBridge/Private/Tests"
    target.resolve().relative_to(host.resolve())
    shutil.copytree(source, target)
    identity = write_identity(plugin, ROOT)
    records = dict((file.relative_to(source).as_posix(), hashlib.sha256(file.read_bytes()).hexdigest())
                   for file in source.rglob("*") if file.is_file())
    logs = host / "Logs"
    logs.mkdir(parents=True, exist_ok=True)
    (logs / "NativeTestBuild.json").write_text(
        json.dumps(dict(identity=identity, test_sources=records), indent=2) + "\n", encoding="utf-8")
    print(f"Prepared native automation host: {host}")


if __name__ == "__main__":
    prepare()
