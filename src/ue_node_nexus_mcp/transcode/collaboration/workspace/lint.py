"""Lint the selected worktree snapshots using typed diagnostics and cached schema."""

from __future__ import annotations

from pathlib import Path

from ...lint import lint_document
from ...parser import parse
from ...paths import display_path, iter_text_files, parse_text_path
from ...sync_files import read_text
from ...sync_project import SyncError
from ..semantic.decode import to_document
from .files import capture_files


def report(rows: list[dict], diagnostics: list, **extra) -> dict:
    return dict(rows=rows, diagnostics=[item.format() for item in diagnostics],
                ok_files=sum(1 for row in rows if row["errors"] == 0),
                error_count=sum(item.severity == "error" for item in diagnostics),
                warning_count=sum(item.severity == "warning" for item in diagnostics), **extra)


def lint(workspace, paths) -> dict:
    entries, files, selected = capture_files(workspace, paths)
    diagnostics, rows = [], []
    for asset in selected:
        snapshot = workspace.store.objects.data(entries[asset], "snapshot")
        sink = lint_document(to_document(snapshot), snapshot["semantic"]["kind"], workspace.schema,
                             file=files[asset], current_key=workspace.schema.key if workspace.schema else None)
        diagnostics.extend(sink.items)
        rows.append(dict(asset=asset, file=files[asset], errors=len(sink.errors()), warnings=len(sink.items) - len(sink.errors())))
    return report(rows, diagnostics)


def lint_root(context, files_root: str, paths: list[str] | None = None) -> dict:
    """Lint mirror files in a directory no workspace has to own.

    Offline validation is the one thing that needs neither an editor nor a
    registered mirror: a hand-prepared tree of ``.nexus`` files is checkable
    against the cached schema exactly as a worktree is.
    """
    root = Path(files_root).expanduser()
    if not root.is_dir():
        raise SyncError("invalid_option", "files_root must be an existing directory", dict(files_root=str(root)))
    wanted = set(paths or ())
    diagnostics, rows, skipped = [], [], []
    for file in iter_text_files(root):
        parsed = parse_text_path(root, file)
        label = display_path(root, file)
        if parsed is None:
            skipped.append(label)
            continue
        asset, kind = parsed
        if wanted and not (label in wanted or asset in wanted):
            continue
        document, sink = parse(read_text(file) or "", file=label)
        if not sink.has_errors:
            sink.extend(lint_document(document, kind, context.schema, label, context.schema_key or None))
        diagnostics.extend(sink.items)
        rows.append(dict(asset=asset, kind=kind, file=label, errors=len(sink.errors()), warnings=len(sink.items) - len(sink.errors())))
    return report(rows, diagnostics, action="lint", files_root=str(root), skipped=skipped,
                  schema_available=bool(context.schema and context.schema.available))
