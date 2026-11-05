"""A crash leaves a modal restore prompt that no bridge request can answer."""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
import shutil
import time

RESTORE_NAME = "PackageRestoreData.json"
BACKUP_DIR = "Saved/Nexus/PackageRecovery"
MODES = ("decline", "prompt")
LOG = logging.getLogger(__name__)


def autosave_dir(project_path: str | Path) -> Path:
    return Path(project_path).parent / "Saved" / "Autosaves"


def restore_file(project_path: str | Path) -> Path:
    return autosave_dir(project_path) / RESTORE_NAME


def index_text(raw: bytes) -> str:
    """UE writes the index through ``TJsonWriter<TCHAR>``: UTF-16LE without a BOM on Windows."""
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        return raw.decode("utf-16")
    if len(raw) >= 2 and raw[1] == 0:
        return raw.decode("utf-16-le")
    if len(raw) >= 2 and raw[0] == 0:
        return raw.decode("utf-16-be")
    return raw.decode("utf-8-sig")


def pending(project_path: str | Path) -> list[dict]:
    """Packages UE will offer to restore; empty once a clean shutdown cleared the file."""
    path = restore_file(project_path)
    if not path.is_file():
        return []
    try:
        data = json.loads(index_text(path.read_bytes()))
    except (OSError, ValueError):
        return []
    if not data.get("RestoreEnabled"):
        return []
    rows = data.get("Packages") or []
    return [dict(package=row.get("PackagePathName"), asset=row.get("PackageAssetName"), autosave=row.get("AutoSavePath"))
            for row in rows if isinstance(row, dict)]


def mode() -> str:
    """``decline`` answers the prompt before it opens; ``prompt`` leaves it to a human."""
    value = os.environ.get("UE_NEXUS_PACKAGE_RECOVERY", "decline").strip().lower()
    return value if value in MODES else "decline"


def preserve(project_path: str | Path) -> str:
    """Copy the restore index and the autosaves it names before the launch declines them."""
    rows = pending(project_path)
    target = Path(project_path).parent / BACKUP_DIR / time.strftime("%Y%m%d-%H%M%S")
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(restore_file(project_path), target / RESTORE_NAME)
    saved = []
    for row in rows:
        name = row.get("autosave")
        if not isinstance(name, str) or not name:
            continue
        source = autosave_dir(project_path) / name
        if source.is_file():
            shutil.copy2(source, target / Path(name).name)
            saved.append(Path(name).name)
    (target / "pending.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False), encoding="utf-8")
    LOG.info("package recovery preserved at %s files=%d", target, len(saved))
    return str(target)


def launch_flag(project_path: str | Path) -> list[str]:
    """Answer the modal up front: it blocks the game thread, so no requested call can dismiss it."""
    if mode() != "decline" or not pending(project_path):
        return []
    try:
        preserve(project_path)
    except OSError as error:
        # Never discard restore data we could not copy aside.
        LOG.warning("package recovery could not be preserved, leaving the prompt to the user: %s", error)
        return []
    return ["-AutoDeclinePackageRecovery"]
