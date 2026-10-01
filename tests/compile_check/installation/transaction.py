"""Stage a complete plugin set, then promote with reversible directory renames."""

import json
from pathlib import Path
from uuid import uuid4

from tests.compile_check.prepare_host import sync_directory


def contained(path: Path, engine: Path) -> Path:
    path.resolve().relative_to(engine.resolve())
    if path.is_symlink() or (path.exists() and getattr(path.lstat(), "st_file_attributes", 0) & 0x400):
        raise ValueError(f"engine installation must use real directories: {path}")
    return path


def install_verified(host: Path, engine: Path, plugins, verify) -> Path:
    work = contained(engine / ".nexus-plugin-install" / uuid4().hex, engine)
    staged = work / "staged"
    for name in plugins:
        source = host / "Plugins" / name
        target = contained(staged / name, engine)
        target.mkdir(parents=True)
        for folder in ("Binaries", "Source", "Config", "Resources"):
            if (source / folder).is_dir():
                sync_directory(source / folder, target / folder, engine)
        for file in (f"{name}.uplugin", "BuildIdentity.json"):
            (target / file).write_bytes((source / file).read_bytes())
        verify(source, target)

    marketplace = contained(engine / "Engine/Plugins/Marketplace", engine)
    originals = []
    for group in ("Editor", "Marketplace"):
        for name in plugins:
            original = contained(engine / "Engine/Plugins" / group / name, engine)
            if original.exists():
                originals.append((original, contained(work / "backup" / group / name, engine)))
    journal = dict(phase="staged", backups=[dict(original=str(a), backup=str(b)) for a, b in originals],
                   targets=[str(marketplace / name) for name in plugins])

    def record(phase):
        journal["phase"] = phase
        (work / "receipt.json").write_text(json.dumps(journal, indent=2) + "\n", encoding="utf-8")

    record("staged")
    moved, promoted = [], []
    try:
        for original, backup in originals:
            backup.parent.mkdir(parents=True, exist_ok=True)
            original.rename(backup)
            moved.append((original, backup))
        record("backed_up")
        marketplace.mkdir(parents=True, exist_ok=True)
        for name in plugins:
            target = contained(marketplace / name, engine)
            (staged / name).rename(target)
            promoted.append((staged / name, target))
        record("installed")
    except Exception:
        for source, target in reversed(promoted):
            target.rename(source)
        for original, backup in reversed(moved):
            backup.rename(original)
        record("rolled_back")
        raise
    return work / "receipt.json"
