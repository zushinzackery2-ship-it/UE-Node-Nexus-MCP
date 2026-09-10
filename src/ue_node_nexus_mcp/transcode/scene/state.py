"""Three-way state for scene groups, kept separate from asset package state."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from ..state import sha256_text
from ..sync_files import write_text_atomic
from ..sync_project import now_iso, SyncError
from .paths import identity, state_path


@dataclass
class SceneRecord:
    key: str
    map_path: str
    name: str
    file: str
    base_file: str
    base_hash: str = ""
    live_revision: str = ""
    synced_at: str = ""
    file_hash: str = ""

    def to_json(self) -> dict[str, Any]:
        return {key: value for key, value in self.__dict__.items() if key != "key"}

    @classmethod
    def from_json(cls, key: str, value: dict[str, Any]) -> "SceneRecord":
        return cls(key, str(value.get("map_path", "")), str(value.get("name", "")), str(value.get("file", "")), str(value.get("base_file", "")), str(value.get("base_hash", "")), str(value.get("live_revision", "")), str(value.get("synced_at", "")), str(value.get("file_hash", "")))


@dataclass
class SceneState:
    records: dict[str, SceneRecord] = field(default_factory=dict)

    @classmethod
    def load(cls, project: Path) -> "SceneState":
        path = state_path(project)
        if not path.is_file():
            return cls()
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise SyncError("scene_state_invalid", f"cannot read {path}") from exc
        if not isinstance(payload, dict) or payload.get("version") not in (1, 2) or not isinstance(payload.get("scenes"), dict):
            raise SyncError("scene_state_invalid", f"invalid scene state: {path}")
        records = dict()
        for key, value in payload["scenes"].items():
            if not isinstance(value, dict):
                raise SyncError("scene_state_invalid", f"invalid record: {key}")
            item = SceneRecord.from_json(key, value)
            if identity(item.map_path, item.name) != key or not item.file or not item.base_file:
                raise SyncError("scene_state_invalid", f"invalid identity: {key}")
            for name in (item.file, item.base_file):
                if not (project / name).resolve().is_relative_to(project.resolve()):
                    raise SyncError("scene_state_invalid", f"path leaves mirror project: {name}")
            records[key] = item
        return cls(records)

    def serialize(self) -> str:
        scenes = dict((key, value.to_json()) for key, value in sorted(self.records.items()))
        return json.dumps(dict(version=2, scenes=scenes), indent=1, ensure_ascii=False)

    def save(self, project: Path) -> None:
        write_text_atomic(state_path(project), self.serialize())

    def put(self, record: SceneRecord) -> None:
        self.records[record.key] = record


def record(project: Path, key: str, raw: dict[str, Any], file: Path, base: Path, file_hash: str = "") -> SceneRecord:
    return SceneRecord(key, str(raw["map_path"]), str(raw["name"]), file.relative_to(project).as_posix(), base.relative_to(project).as_posix(), sha256_text(json.dumps(raw, sort_keys=True, ensure_ascii=False)), str(raw.get("revision", "")), now_iso(), file_hash)
