"""Coordinate scene groups with the existing asset synchronization pipeline."""

from __future__ import annotations

from typing import Any

from ..parser import parse
from ..sync_deps import document_dependencies
from ..sync_files import mirrored_assets, read_text
from ..sync_project import BridgeCall, ProjectContext, SyncError, ensure_schema
from ..sync_push import FAILED_ACTIONS
from .files import read_base, read_document
from .lint import lint_scenes
from .pull import pull_scenes
from .push import desired_scene, push_scenes
from .selection import check_membership, select
from .state import SceneState
from .status import status_scenes


class SceneBatch:
    def __init__(self, context: ProjectContext, paths: list[str] | None, options: dict[str, Any], action: str):
        self.context = context
        self.options = options
        self.state = SceneState.load(context.project)
        self.asset_paths, self.items = (paths, []) if action == "schema" else select(context, self.state, paths, options)
        if action == "push" and self.items and options.get("force") == "ue":
            raise SyncError("scene_pull_required", "use pull(force=ue) to adopt scene identity before pushing")
        self.dependencies: dict[str, set[str]] = dict()
        self.errors: dict[str, str] = dict()

    def prepare_push(self) -> list[str] | None:
        local = mirrored_assets(self.context.project)
        required = set()
        claimed = dict()
        for item in self.items:
            try:
                base = read_base(self.context.project, item)
                text, desired, _ = desired_scene(self.context, item, base, bool(self.options.get("allow_delete")))
                check_membership(self.context.project, self.state, item, desired)
                for actor in desired["actors"]:
                    key = (item.map_path, actor["id"])
                    if key in claimed and claimed[key] != item.key:
                        raise SyncError("scene_membership_conflict", f"actor is also selected by {claimed[key]}")
                    claimed[key] = item.key
                dependencies = set()
                if text is not None:
                    document, _, _ = read_document(self.context.project, self.context.project / item.file)
                    dependencies = document_dependencies(document, "scene")
                self.dependencies[item.key] = self._closure(dependencies, local)
                required.update(self.dependencies[item.key] & set(local))
            except (OSError, ValueError, SyncError) as exc:
                self.errors[item.key] = str(exc)
        if self.asset_paths is None:
            return None
        return sorted(set(self.asset_paths) | required)

    @staticmethod
    def _closure(dependencies: set[str], local: dict) -> set[str]:
        found = set(dependencies)
        pending = list(dependencies & set(local))
        while pending:
            asset = pending.pop()
            kind, file = local[asset]
            document, sink = parse(read_text(file) or "")
            if sink.has_errors:
                continue
            new = document_dependencies(document, kind) - found
            found.update(new)
            pending.extend(new & set(local))
        return found

    def abort_assets(self) -> bool:
        return bool(self.errors) and not self.options.get("dry_run", True) and bool(self.options.get("stop_on_error", True))

    def run(self, bridge: BridgeCall, action: str, report: dict[str, Any]) -> None:
        if not self.items:
            return
        if action in ("pull", "init", "push") and self.context.bridge_available:
            ensure_schema(bridge, self.context)
        if action in ("pull", "init"):
            result = pull_scenes(bridge, self.context, self.state, self.items, self.options)
        elif action == "status":
            result = status_scenes(bridge, self.context, self.items, bool(self.options.get("include_clean")))
        elif action == "lint":
            result = lint_scenes(self.context, self.items)
        elif action == "push":
            asset_rows = [row for row in report.get("rows", []) if isinstance(row, dict) and "asset" in row]
            blocked = set(row["asset"] for row in asset_rows if row.get("action") in FAILED_ACTIONS
                          or (row.get("action") == "skipped" and row.get("state") not in ("clean", "ue-new")))
            deferred = set(row["asset"] for row in asset_rows if row.get("action") == "planned")
            stop_on_error = not self.options.get("dry_run", True) and bool(self.options.get("stop_on_error", True))
            stopped = bool(report.get("stopped")) or self.abort_assets() or (stop_on_error and bool(blocked))
            result = push_scenes(bridge, self.context, self.state, self.items, self.options,
                                 self.dependencies, blocked, stopped, self.errors, deferred)
        else:
            return
        self._merge(report, result)

    @staticmethod
    def _merge(report: dict[str, Any], result: dict[str, Any]) -> None:
        rows = result.get("rows", [])
        counts = result.get("counts", dict())
        if not counts:
            for row in rows:
                name = row.get("action", row.get("state", "linted"))
                counts[name] = counts.get(name, 0) + 1
        has_assets = bool(report.get("rows")) or bool(report.get("counts"))
        report["total"] = report.get("total", len(report.get("rows", []))) + result.get("total", len(rows))
        report["scene_counts"] = counts
        report["scene_total"] = result.get("total", len(rows))
        report["scene_rows" if has_assets else "rows"] = rows
        if not has_assets:
            report.pop("columns", None)
        combined = dict(report.get("counts", dict()))
        for key, value in counts.items():
            combined[key] = combined.get(key, 0) + value
        report["counts"] = combined
        for key in ("error_count", "warning_count", "ok_files"):
            report[key] = report.get(key, 0) + result.get(key, 0)
        report["diagnostics"] = report.get("diagnostics", []) + result.get("diagnostics", [])
        report["stopped"] = bool(report.get("stopped")) or bool(result.get("stopped"))
