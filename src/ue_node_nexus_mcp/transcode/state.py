"""Per-project sync state (``.nexus/state.json``) and the three-way status matrix."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .paths import state_path

STATE_VERSION = 1
STATES = ("clean", "local-modified", "ue-modified", "both-modified", "local-new", "ue-new", "local-deleted", "ue-deleted", "unknown")


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


@dataclass
class AssetState:
    asset_path: str
    kind: str
    file: str
    base_hash: str = ""
    ue_saved_hash: str = ""
    ue_dirty: bool = False
    synced_at: str = ""

    def to_json(self) -> dict[str, Any]:
        return {
            "kind": self.kind,
            "file": self.file,
            "base_hash": self.base_hash,
            "ue_saved_hash": self.ue_saved_hash,
            "ue_dirty": self.ue_dirty,
            "synced_at": self.synced_at,
        }

    @classmethod
    def from_json(cls, asset_path: str, data: dict[str, Any]) -> AssetState:
        return cls(
            asset_path=asset_path,
            kind=str(data.get("kind", "")),
            file=str(data.get("file", "")),
            base_hash=str(data.get("base_hash", "")),
            ue_saved_hash=str(data.get("ue_saved_hash", "")),
            ue_dirty=bool(data.get("ue_dirty", False)),
            synced_at=str(data.get("synced_at", "")),
        )


@dataclass
class SyncState:
    assets: dict[str, AssetState] = field(default_factory=dict)

    @classmethod
    def load(cls, project: Path) -> SyncState:
        path = state_path(project)
        if not path.is_file():
            return cls()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return cls()
        assets = data.get("assets") if isinstance(data, dict) else None
        state = cls()
        for asset_path, record in (assets or {}).items():
            if isinstance(record, dict):
                state.assets[asset_path] = AssetState.from_json(asset_path, record)
        return state

    def save(self, project: Path) -> None:
        path = state_path(project)
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_suffix(path.suffix + ".tmp")
        temp.write_text(self.serialize(), encoding="utf-8")
        temp.replace(path)

    def serialize(self) -> str:
        payload = dict(version=STATE_VERSION, assets=dict((key, value.to_json()) for key, value in sorted(self.assets.items())))
        return json.dumps(payload, indent=1, ensure_ascii=False)

    def get(self, asset_path: str) -> AssetState | None:
        return self.assets.get(asset_path)

    def put(self, state: AssetState) -> None:
        self.assets[state.asset_path] = state

    def remove(self, asset_path: str) -> None:
        self.assets.pop(asset_path, None)


@dataclass
class UeAssetInfo:
    asset_path: str
    class_path: str
    kind: str
    saved_hash: str
    dirty: bool


def classify(
    state: AssetState | None,
    local_hash: str | None,
    ue: UeAssetInfo | None,
    ue_known: bool,
) -> str:
    """Three-way classification.

    ``local_hash`` None means the text file is missing; ``ue`` None means the
    asset is missing on the UE side (only meaningful when ``ue_known``).
    """
    if state is None:
        if local_hash is None:
            return "ue-new" if ue is not None else "unknown"
        if ue_known and ue is not None:
            # a hand-written file for an asset that was never pulled: needs a base first
            return "both-modified"
        return "local-new"
    local_changed = local_hash is not None and local_hash != state.base_hash
    if local_hash is None:
        if ue_known and ue is None:
            return "ue-deleted"
        return "local-deleted"
    if not ue_known:
        return "local-modified" if local_changed else "unknown"
    if ue is None:
        return "ue-deleted"
    ue_changed = ue.dirty or (ue.saved_hash and ue.saved_hash != state.ue_saved_hash)
    if local_changed and ue_changed:
        return "both-modified"
    if local_changed:
        return "local-modified"
    if ue_changed:
        return "ue-modified"
    return "clean"
