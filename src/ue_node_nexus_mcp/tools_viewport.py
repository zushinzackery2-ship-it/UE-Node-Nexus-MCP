"""Two-phase editor viewport screenshot operations.

The capture request is a bridge write that returns the target PNG path
immediately; the file is written asynchronously after the editor's next
viewport redraw. Because the UE editor and this MCP server share one machine
(named-pipe transport), completion is observed with a local file check instead
of a second bridge roundtrip.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def viewport_capture(
    filename: str | None = None,
    show_ui: bool = False,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Request an async editor viewport screenshot; returns the target PNG path immediately."""
    if filename is not None:
        require_non_empty_string(filename, "filename")
    return _call(
        "viewport_capture",
        {
            "filename": filename,
            "show_ui": show_ui,
            "dry_run": dry_run,
        },
    )


@default_tool()
def viewport_capture_status(file_path: str) -> dict[str, Any]:
    """MCP-local check whether a previously requested screenshot file exists on disk yet."""
    require_non_empty_string(file_path, "file_path")
    path = Path(file_path)
    exists = path.is_file()
    data: dict[str, Any] = {"file_path": file_path, "exists": exists}
    if exists:
        stat = path.stat()
        data["size_bytes"] = stat.st_size
        data["modified_at"] = stat.st_mtime
    return {
        "ok": True,
        "operation": "viewport_capture_status",
        "data": data,
        "diagnostics": [],
        "warnings": [],
    }
