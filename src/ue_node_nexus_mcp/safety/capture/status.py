"""Associate a screenshot with its durable request and verify actual pixels."""

from __future__ import annotations

import json
import zlib
from pathlib import Path

from ...instances.identity.processes import is_alive
from .png import inspect_png


def status(file_path: str, capture_id: str | None = None) -> dict:
    path = Path(file_path).resolve()
    receipt_path = Path(str(path) + ".capture.json")
    data = dict(file_path=str(path), exists=path.is_file(), done=False, state="untracked")
    if not receipt_path.is_file():
        if path.is_file():
            data.update(size_bytes=path.stat().st_size, modified_at=path.stat().st_mtime)
        return envelope(True, data)
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
        if not isinstance(receipt, dict) or receipt.get("capture_protocol") != 1:
            raise ValueError("unsupported capture receipt")
        identifier = receipt.get("capture_id")
        if not isinstance(identifier, str) or len(identifier) != 32 or any(char not in "0123456789abcdef" for char in identifier.lower()):
            raise ValueError("invalid capture identity")
        if capture_id is not None and (not isinstance(capture_id, str) or capture_id.lower() != identifier.lower()):
            raise ValueError("receipt belongs to another capture")
        if identifier.lower() not in path.stem.lower() or Path(receipt["file_path"]).resolve() != path:
            raise ValueError("capture artifact identity mismatch")
        state = receipt.get("state")
        if state not in ("queued", "rendering", "completed", "failed"):
            raise ValueError("invalid capture phase")
        data.update(receipt, file_path=str(path), exists=path.is_file(), done=False)
        if state in ("queued", "rendering"):
            producer = receipt.get("producer")
            if not isinstance(producer, dict) or not all(key in producer for key in ("pid", "process_created", "executable")):
                raise ValueError("capture producer identity is missing")
            if not is_alive(producer):
                data.update(state="interrupted", error_code="capture_interrupted", done=True)
                return envelope(False, data, "capture_interrupted")
            return envelope(True, data)
        data["done"] = True
        if state == "failed":
            return envelope(False, data, receipt.get("error_code") or "capture_failed")
        measured = inspect_png(path)
        for field in ("width", "height", "size_bytes", "sha1"):
            if measured[field] != receipt.get(field):
                raise ValueError("capture artifact differs from its completion receipt: " + field)
        data.update(measured)
        return envelope(True, data)
    except (OSError, KeyError, TypeError, ValueError, zlib.error) as error:
        data.update(state="failed", done=True, error_code="capture_artifact_invalid", reason=str(error))
        return envelope(False, data, "capture_artifact_invalid")


def envelope(ok: bool, data: dict, code: str | None = None) -> dict:
    result = dict(ok=ok, operation="viewport_capture_status", data=data, diagnostics=[], warnings=[])
    if code:
        result["error"] = dict(code=code, message=data.get("reason") or "screenshot did not complete successfully")
    return result
