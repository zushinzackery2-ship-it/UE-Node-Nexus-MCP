"""``ue_sync status``: three-way classification of every selected asset."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .paths import object_path, parse_text_path
from .state import AssetState, SyncState, UeAssetInfo, classify
from .sync_files import canonical_hash, mirrored_assets, read_text
from .sync_project import BridgeCall, ProjectContext, SyncError, call_ok

MIRRORED_KINDS = ("material", "material_function", "material_instance", "blueprint", "niagara_system", "niagara_emitter", "asset")


@dataclass
class AssetStatus:
    asset_path: str
    kind: str
    state: str
    file: Path | None
    ue: UeAssetInfo | None
    record: AssetState | None
    local_hash: str | None

    def row(self) -> list[Any]:
        return [self.asset_path, self.kind, self.state]


def resolve_selection(context: ProjectContext, paths: list[str] | None, state: SyncState) -> list[str]:
    """Turn user paths (asset paths, mirror files or directories) into object paths."""
    if not paths:
        selected = set(mirrored_assets(context.project))
        selected.update(state.assets)
        return sorted(selected)
    selected: list[str] = []
    for item in paths:
        text = item.strip()
        if not text:
            continue
        if text.startswith("/Game/") or text.startswith("/Game"):
            selected.append(object_path(text))
            continue
        path = Path(text)
        if not path.is_absolute():
            path = context.project / path
        if path.is_dir():
            for asset_path, (_, file) in mirrored_assets(context.project).items():
                if file.resolve().is_relative_to(path.resolve()):
                    selected.append(asset_path)
            continue
        parsed = parse_text_path(context.project, path)
        if parsed is None:
            raise SyncError("invalid_path", f"not a /Game asset path or a mirror file: {item}")
        selected.append(parsed[0])
    return sorted(set(selected))


def query_ue(bridge: BridgeCall, context: ProjectContext, asset_paths: list[str], discover: bool = False, include_stubs: bool = False) -> tuple[dict[str, UeAssetInfo], bool]:
    """Ask UE for saved hash / dirty flags; returns (infos, known)."""
    if not context.bridge_available:
        return {}, False
    payload: dict[str, Any] = {"asset_paths": asset_paths, "discover": discover, "include_stubs": include_stubs}
    try:
        data = call_ok(bridge, "transcode_status", payload).get("data") or {}
    except SyncError as exc:
        context.warnings.append(f"transcode_status failed: {exc}")
        return {}, False
    infos: dict[str, UeAssetInfo] = {}
    for row in data.get("assets") or []:
        if not isinstance(row, list) or len(row) < 5:
            continue
        asset_path = object_path(str(row[0]))
        infos[asset_path] = UeAssetInfo(asset_path=asset_path, class_path=str(row[1]), kind=str(row[2]), saved_hash=str(row[3]), dirty=bool(row[4]))
    return infos, True


def compute_status(context: ProjectContext, state: SyncState, asset_paths: list[str], ue_infos: dict[str, UeAssetInfo], ue_known: bool) -> list[AssetStatus]:
    local = mirrored_assets(context.project)
    statuses: list[AssetStatus] = []
    for asset_path in asset_paths:
        record = state.get(asset_path)
        kind, file = local.get(asset_path, (record.kind if record else "", None))
        ue = ue_infos.get(asset_path)
        if not kind and ue is not None:
            kind = ue.kind
        text = read_text(file) if file is not None else None
        local_hash = canonical_hash(text) if text is not None else None
        classification = classify(record, local_hash, ue, ue_known)
        statuses.append(AssetStatus(asset_path=asset_path, kind=kind, state=classification, file=file, ue=ue, record=record, local_hash=local_hash))
    return statuses
