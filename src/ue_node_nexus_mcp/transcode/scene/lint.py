"""Offline scene diagnostics validate the model and incremental edit contract."""

from __future__ import annotations

from typing import Any

from ..sync_project import ProjectContext, SyncError
from .backend import selector
from .diff import build_plan
from .files import read_base, read_document
from .model import from_document
from .state import SceneRecord


def lint_scenes(context: ProjectContext, items: list[SceneRecord]) -> dict[str, Any]:
    rows, diagnostics = [], []
    for item in items:
        errors = []
        try:
            document, _, sink = read_document(context.project, context.project / item.file)
            errors.extend(message.format() for message in sink.errors())
            if not errors:
                base = read_base(context.project, item)
                desired, _ = from_document(document, base)
                if base:
                    build_plan(base, desired, selector(item, base), True)
                if document.header.schema and context.schema_key and document.header.schema != context.schema_key:
                    raise SyncError("schema_stale", "scene schema differs from the project lock")
        except (OSError, ValueError, SyncError) as exc:
            code = exc.code if isinstance(exc, SyncError) else "scene_read_failed"
            errors.append(f"{item.file}: {code}: {exc}")
        diagnostics.extend(errors)
        rows.append(dict(scene=item.key, file=item.file, kind="scene", errors=len(errors)))
    return dict(rows=rows, diagnostics=diagnostics, error_count=len(diagnostics), warning_count=0,
                ok_files=sum(row["errors"] == 0 for row in rows))
