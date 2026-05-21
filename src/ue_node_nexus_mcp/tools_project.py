from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def project_input_mappings_get(
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Read legacy Project Settings input action and axis mappings."""
    return _call(
        "project_input_mappings_get",
        {
            "format": format,
        },
    )


@default_tool()
def project_input_mappings_patch(
    operations: list[dict[str, Any]],
    dry_run: bool = True,
    save_config: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Add or remove legacy Project Settings input action and axis mappings."""
    require_list(operations, "operations")
    return _call(
        "project_input_mappings_patch",
        {
            "operations": operations,
            "dry_run": dry_run,
            "save_config": save_config,
            "format": format,
        },
    )
