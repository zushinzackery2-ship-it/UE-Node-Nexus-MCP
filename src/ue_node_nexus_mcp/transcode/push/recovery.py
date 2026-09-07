"""Durable evidence for incomplete pushes, separate from the accepted base."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..paths import asset_relative, display_path, pending_dir
from ..sync_files import read_json, write_json
from ..sync_project import ProjectContext, now_iso
from .model import Prepared


def recovery_path(project: Path, asset: str) -> Path:
    relative = asset_relative(asset)
    if relative is None:
        raise ValueError(f"only /Game assets support push recovery: {asset}")
    return pending_dir(project).joinpath(*relative.parts).with_suffix(".push.json")


def load_recovery(project: Path, asset: str) -> dict[str, Any] | None:
    return read_json(recovery_path(project, asset))


def record_failure(context: ProjectContext, item: Prepared, response: Any, errors: list[str]) -> str:
    data = response.get("data") if isinstance(response, dict) else None
    data = data if isinstance(data, dict) else dict()
    ids = dict((item.base or dict()).get("ids") or dict())
    ids.update((guid, identifier) for identifier, guid in item.plan.ids.items())
    ids.update((guid, identifier) for identifier, guid in (data.get("id_map") or dict()).items())
    path = recovery_path(context.project, item.status.asset_path)
    write_json(path, dict(
        asset_path=item.status.asset_path, kind=item.status.kind, time=now_iso(),
        source=display_path(context.project, item.file), local_text=item.text, ids=ids,
        plan=item.plan.to_payload(), response=response, errors=errors,
    ))
    return display_path(context.project, path)
