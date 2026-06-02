from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def sound_cue_summary_get(
    asset_path: str,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Return SoundCue USoundNode tree nodes, edges, and common node parameters."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "sound_cue_summary_get",
        {
            "asset_path": asset_path,
            "format": format,
        },
    )
