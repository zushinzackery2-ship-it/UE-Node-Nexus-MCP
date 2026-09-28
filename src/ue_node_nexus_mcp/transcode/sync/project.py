"""Session context for sync: mirror root, bound UE project, schema lock, bridge calls."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from ..errors import SyncError

from ..storage.paths import project_dir, project_info_path, project_label, resolve_root
from ..schema.lock import SchemaLock, load_schema_lock

BridgeCall = Callable[[str, dict[str, Any]], dict[str, Any]]
NIAGARA_KINDS = ("niagara_system", "niagara_emitter")


@dataclass
class ProjectContext:
    root: Path
    project_name: str
    project: Path
    schema_key: str
    engine_version: str = ""
    project_file: str = ""
    bridge_available: bool = True
    bridge_contacted: bool = True
    schema: SchemaLock | None = None
    warnings: list[str] = field(default_factory=list)

    def info(self) -> dict[str, Any]:
        result = dict(root=str(self.root), project=self.project_name, project_dir=str(self.project),
                      schema_key=self.schema_key, schema_available=bool(self.schema and self.schema.available),
                      bridge_contacted=self.bridge_contacted, engine_version=self.engine_version)
        if self.bridge_contacted:
            result["bridge_available"] = self.bridge_available
        return result


def now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def call_ok(bridge: BridgeCall, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    response = bridge(operation, payload)
    if not isinstance(response, dict) or response.get("ok") is not True:
        error = (response or {}).get("error") if isinstance(response, dict) else None
        code = str((error or {}).get("code", "bridge_error"))
        message = str((error or {}).get("message", f"{operation} failed"))
        raise SyncError(code, message, {"operation": operation, "error": error})
    return response


def _data(response: dict[str, Any]) -> dict[str, Any]:
    data = response.get("data")
    return data if isinstance(data, dict) else {}


def resolve_context(bridge: BridgeCall, env: dict[str, str] | None = None, cwd: Path | None = None, require_bridge: bool = False,
                    project_hint: str | None = None, offline: bool = False) -> ProjectContext:
    """Bind the mirror root to the currently bound UE editor (or the last known project)."""
    root = resolve_root(env, cwd)
    project_name = ""
    schema_key = ""
    engine_version = ""
    project_file = ""
    bridge_available = not offline
    warnings: list[str] = []
    if not offline:
        try:
            context = _data(call_ok(bridge, "project_context_get", {}))
            project_name = str(context.get("project_name", ""))
            project_file = str(context.get("project_file_path", ""))
            capabilities = _data(call_ok(bridge, "bridge_capabilities_get", {}))
            schema_key = str(capabilities.get("schema_key", ""))
            engine_version = str(capabilities.get("engine_version", ""))
        except SyncError as exc:
            bridge_available = False
            warnings.append(f"UE bridge unavailable: {exc}")
            if require_bridge:
                raise
    hint = project_label(project_hint)
    if not project_name:
        project_name = hint or stored_project_name(root)
        if not project_name:
            raise SyncError("no_project", "no UE editor is bound and no mirrored project exists yet; start the editor and run ue_sync init")
    if hint and os.path.normcase(hint) != os.path.normcase(project_name):
        raise SyncError("project_mismatch", "the bound editor is for another project",
                        dict(requested=project_hint, resolved=hint, actual=project_name))
    project_name = hint or project_name
    project = project_dir(root, project_name)
    stored = _read_project_info(project)
    if not schema_key:
        schema_key = str(stored.get("schema_key", ""))
    if not engine_version:
        engine_version = str(stored.get("engine_version", ""))
    schema = load_schema_lock(root, schema_key) if schema_key else None
    if schema is not None and not schema.available and not schema_key:
        schema = None
    return ProjectContext(
        root=root,
        project_name=project_name,
        project=project,
        schema_key=schema_key or (schema.key if schema else ""),
        engine_version=engine_version,
        project_file=project_file or str(stored.get("project_file", "")),
        bridge_available=bridge_available,
        bridge_contacted=not offline,
        schema=schema,
        warnings=warnings,
    )


def write_project_info(context: ProjectContext, extra: dict[str, Any] | None = None) -> None:
    context.project.mkdir(parents=True, exist_ok=True)
    stored = _read_project_info(context.project)
    stored.update({
        "project_name": context.project_name,
        "project_file": context.project_file,
        "schema_key": context.schema_key,
        "engine_version": context.engine_version,
        "updated_at": now_iso(),
    })
    if extra:
        stored.update(extra)
    project_info_path(context.project).write_text(json.dumps(stored, indent=1, ensure_ascii=False), encoding="utf-8")


def _read_project_info(project: Path) -> dict[str, Any]:
    path = project_info_path(project)
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def stored_project_name(root: Path) -> str:
    if not root.is_dir():
        return ""
    candidates = [path for path in root.iterdir() if path.is_dir() and project_info_path(path).is_file()]
    if not candidates:
        return ""
    if len(candidates) != 1:
        raise SyncError("project_required", "multiple mirrored projects exist; bind an editor to select its project")
    return str(_read_project_info(candidates[0]).get("project_name") or candidates[0].name)


def ensure_root_registered(bridge: BridgeCall, context: ProjectContext) -> dict[str, Any]:
    context.root.mkdir(parents=True, exist_ok=True)
    data = _data(call_ok(bridge, "transcode_root_set", {"root": str(context.root)}))
    if not context.schema_key and data.get("schema_key"):
        context.schema_key = str(data["schema_key"])
    return data


def export_operation(kind: str) -> str:
    return "vfx_transcode_export" if kind in NIAGARA_KINDS else "transcode_export"


def apply_operation(kind: str) -> str:
    return "vfx_transcode_apply" if kind in NIAGARA_KINDS else "transcode_apply"
