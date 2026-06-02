from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def texture_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return Texture2D dimensions, pixel/source formats, compression, sRGB, and LOD group.

    Exposes width/height (cooked) plus imported_width/height, source_width/height,
    source_format (editor source), compression_format (runtime pixel format),
    compression_settings, srgb, and lod_group as a normalized summary.
    """
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "texture_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )
