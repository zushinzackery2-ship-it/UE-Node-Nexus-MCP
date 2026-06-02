from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def anim_montage_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return AnimMontage sections, slot tracks, segments, and notifies as structured data."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "anim_montage_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )


@default_tool()
def blend_space_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return BlendSpace axis ranges (name/min/max/grid) and animation sample points."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "blend_space_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )
