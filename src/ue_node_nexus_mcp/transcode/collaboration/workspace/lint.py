"""Lint the selected worktree snapshots using typed diagnostics and cached schema."""

from ...lint import lint_document
from ..semantic.decode import to_document
from .files import capture_files


def lint(workspace, paths) -> dict:
    entries, files, selected = capture_files(workspace, paths)
    diagnostics, rows = [], []
    for asset in selected:
        snapshot = workspace.store.objects.data(entries[asset], "snapshot")
        sink = lint_document(to_document(snapshot), snapshot["semantic"]["kind"], workspace.schema,
                             file=files[asset], current_key=workspace.schema.key if workspace.schema else None)
        diagnostics.extend(sink.items)
        rows.append(dict(asset=asset, file=files[asset], errors=len(sink.errors()), warnings=len(sink.items) - len(sink.errors())))
    return dict(diagnostics=[item.format() for item in diagnostics], rows=rows,
                error_count=sum(item.severity == "error" for item in diagnostics),
                warning_count=sum(item.severity == "warning" for item in diagnostics))
