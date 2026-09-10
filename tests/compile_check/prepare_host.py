"""Copy an exact source snapshot into the isolated UBT host."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from release.identity import write_identity

PLUGINS = ("UeNodeNexusBridge", "UeNodeNexusVfxBridge")


def sync_directory(source: Path, target: Path, host: Path) -> None:
    target.resolve().relative_to(host.resolve())
    if target.is_symlink() or source.is_symlink():
        raise ValueError("build inputs and staging directories must be real directories")
    target.mkdir(parents=True, exist_ok=True)
    expected = set()
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"linked build input: {path}")
        relative = path.relative_to(source)
        expected.add(relative)
        destination = target / relative
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        elif not destination.is_file() or path.read_bytes() != destination.read_bytes():
            shutil.copy2(path, destination)
    for path in sorted(target.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        path.resolve().relative_to(host.resolve())
        if path.relative_to(target) not in expected:
            path.rmdir() if path.is_dir() else path.unlink()


def prepare(host: Path) -> None:
    host = host.resolve()
    host.relative_to((ROOT / "build").resolve())
    host.mkdir(parents=True, exist_ok=True)
    for plugin in PLUGINS:
        source = ROOT / "Plugins" / plugin
        target = host / "Plugins" / plugin
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / f"{plugin}.uplugin", target / f"{plugin}.uplugin")
        for name in ("Source", "Config", "Resources"):
            if (source / name).is_dir():
                sync_directory(source / name, target / name, host)
        write_identity(target, ROOT)
    shutil.copy2(Path(__file__).parent / "fixtures/NexusValidation.uproject", host / "NexusValidation.uproject")
    print(f"Prepared isolated build host: {host}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / "build/validation")
    prepare(parser.parse_args().host)
