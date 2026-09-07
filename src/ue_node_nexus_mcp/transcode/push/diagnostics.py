"""Preserve bridge failure semantics and map verb/node diagnostics to source."""

from __future__ import annotations

from typing import Any

from ..errors import Diagnostic
from .model import Prepared, PushResult


def apply_diagnostics(response: Any, item: Prepared, file: str, result: PushResult) -> list[str]:
    start = len(result.diagnostics)
    if not isinstance(response, dict):
        result.diagnostics.append(Diagnostic("error", "invalid_bridge_response", "expected an apply response object", file))
        return [result.diagnostics[-1].format()]
    data = response.get("data")
    data = data if isinstance(data, dict) else dict()
    for failure in data.get("failed") or []:
        if not isinstance(failure, dict):
            continue
        index = failure.get("index")
        line = item.plan.verbs[index].line if isinstance(index, int) and 0 <= index < len(item.plan.verbs) else None
        result.diagnostics.append(Diagnostic(
            "error", str(failure.get("code", "apply_failed")),
            str(failure.get("message", "plan verb failed")), file, line,
        ))
    lines = dict((decl.id, decl.line) for _, decl in item.document.iter_decls())
    ids = dict((guid, identifier) for identifier, guid in item.plan.ids.items())
    ids.update((str(guid), str(identifier)) for identifier, guid in (data.get("id_map") or dict()).items())
    for diagnostic in [*(data.get("diagnostics") or []), *(response.get("diagnostics") or [])]:
        if not isinstance(diagnostic, dict):
            continue
        severity = "error" if str(diagnostic.get("severity", "error")).lower() == "error" else "warning"
        result.diagnostics.append(Diagnostic(
            severity, str(diagnostic.get("code", "compile")), str(diagnostic.get("message", "")),
            file, lines.get(ids.get(str(diagnostic.get("node_id", "")), "")),
        ))
    error = response.get("error")
    error = error if isinstance(error, dict) else dict()
    existing = [entry for entry in result.diagnostics[start:] if entry.severity == "error"]
    if response.get("ok") is not True and (not existing or error.get("code") != "plan_partially_failed"):
        result.diagnostics.append(Diagnostic(
            "error", str(error.get("code", "bridge_error")), str(error.get("message", "transcode apply failed")), file,
        ))
    if not existing and response.get("ok") is True:
        compile_info = data.get("compile") or dict()
        if compile_info.get("ran") and (compile_info.get("ok") is not True or compile_info.get("error_count", 0)):
            result.diagnostics.append(Diagnostic("error", "compile_failed", "asset compile reported errors", file))
        elif data.get("save_error"):
            result.diagnostics.append(Diagnostic("error", "save_failed", str(data["save_error"]), file))
        elif data.get("remaining_errors", 0) or data.get("failed"):
            result.diagnostics.append(Diagnostic("error", "apply_failed", "bridge reports unresolved apply errors", file))
    return [entry.format() for entry in result.diagnostics[start:] if entry.severity == "error"]
