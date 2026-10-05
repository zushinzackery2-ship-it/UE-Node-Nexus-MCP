"""Admit editor screenshot jobs and inspect their verified completion receipts."""

from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def viewport_capture(
    filename: str | None = None,
    target: Literal["level", "active"] = "level",
    show_ui: bool = False,
    dry_run: bool = False,
    timeout_seconds: float = 30,
) -> dict[str, Any]:
    """Screenshot the level editor viewport (or the Slate-active one); returns the absolute PNG path.

    ``target="level"`` is the viewport the user looks through and is what you want
    for "show me the scene"; ``"active"`` is whatever panel Slate last focused,
    which after an editor restart is often a reopened asset editor's preview.
    """
    if filename is not None:
        require_non_empty_string(filename, "filename")
    if not isinstance(timeout_seconds, (int, float)) or isinstance(timeout_seconds, bool) or not 1 <= timeout_seconds <= 120:
        raise ValueError("timeout_seconds must be 1..120")
    return _call(
        "viewport_capture",
        {
            "filename": filename,
            "target": target,
            "show_ui": show_ui,
            "dry_run": dry_run,
            "timeout_seconds": timeout_seconds,
        },
    )


@default_tool()
def viewport_camera_get() -> dict[str, Any]:
    """Read the perspective level editor viewport camera (location, rotation, fov, realtime)."""
    return _call("viewport_camera_get", {})


@default_tool()
def viewport_camera_set(
    location: dict[str, Any] | None = None,
    rotation: dict[str, Any] | None = None,
    look_at: dict[str, Any] | None = None,
    fov: float | None = None,
    dry_run: bool = True,
) -> dict[str, Any]:
    """Move the perspective level editor viewport camera; provide at least one field.

    ``look_at`` aims the camera at a world point from its (new) location and
    conflicts with ``rotation``. Pair with ``viewport_capture`` to shoot the same
    scene from several angles without touching the editor.
    """
    if location is None and rotation is None and look_at is None and fov is None:
        raise ValueError("provide at least one of location / rotation / look_at / fov")
    if rotation is not None and look_at is not None:
        raise ValueError("rotation and look_at both set the view direction; pass one")
    payload: dict[str, Any] = {"dry_run": dry_run}
    for key, value in (("location", location), ("rotation", rotation), ("look_at", look_at), ("fov", fov)):
        if value is not None:
            payload[key] = value
    return _call("viewport_camera_set", payload)


@default_tool()
def viewport_capture_status(file_path: str, capture_id: str | None = None) -> dict[str, Any]:
    """Read a durable screenshot receipt and verify this request's PNG and dimensions."""
    require_non_empty_string(file_path, "file_path")
    from .safety.capture.status import status
    return status(file_path, capture_id)
