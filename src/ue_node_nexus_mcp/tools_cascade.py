from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def cascade_system_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return Cascade particle system emitters, type data, and module stack (read-only)."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "cascade_system_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )
