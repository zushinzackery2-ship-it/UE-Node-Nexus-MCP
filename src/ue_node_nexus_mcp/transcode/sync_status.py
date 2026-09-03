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


def _known_assets(context: ProjectContext, state: SyncState) -> set[str]:
    known = set(mirrored_assets(context.project))
    known.update(state.assets)
    return known


def _expand_game_path(text: str, known: set[str]) -> list[str]:
    """A ``/Game`` path is an asset, a folder of known assets, or both.

    ``/Game/ToonShade`` used to be coerced to the object path ``/Game/ToonShade.ToonShade``
    and then reported as one ``unknown`` asset, so a folder selection silently matched
    nothing. Folder expansion only knows assets already in the mirror or the state;
    an unmirrored asset still has to be named exactly (that is how ``pull`` learns
    about it).
    """
    asset = object_path(text)
    package = asset.rsplit(".", 1)[0]
    prefix = package.rstrip("/") + "/"
    expanded = [path for path in known if path.startswith(prefix)]
    if asset in known or not expanded:
        expanded.append(asset)
    return expanded


def _locate_mirror_path(context: ProjectContext, text: str) -> Path | None:
    """Resolve a filesystem-style path against the places a user actually types."""
    path = Path(text)
    candidates = [path] if path.is_absolute() else [context.project / path, context.root / path, Path.cwd() / path]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def resolve_selection(context: ProjectContext, paths: list[str] | None, state: SyncState) -> list[str]:
    """Turn user paths (asset paths, mirror files or directories) into object paths."""
    known = _known_assets(context, state)
    if not paths:
        return sorted(known)
    selected: list[str] = []
    for item in paths:
        text = item.strip()
        if not text:
            continue
        if text.startswith("/Game"):
            selected.extend(_expand_game_path(text, known))
            continue
        path = _locate_mirror_path(context, text)
        if path is None:
            raise SyncError("invalid_path", f"no such mirror file or directory: {item}", {"tried": ["<absolute>", "<project dir>", "<mirror root>", "<cwd>"]})
        if path.is_dir():
            root = path.resolve()
            selected.extend(asset_path for asset_path, (_, file) in mirrored_assets(context.project).items() if file.resolve().is_relative_to(root))
            continue
        parsed = parse_text_path(context.project, path)
        if parsed is None:
            raise SyncError("invalid_path", f"not a /Game asset path or a mirror file: {item}")
        selected.append(parsed[0])
    if not selected:
        raise SyncError("no_match", "selection matched no mirrored asset", {"paths": list(paths)})
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
