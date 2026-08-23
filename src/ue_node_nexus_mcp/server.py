from __future__ import annotations

from .facade_capabilities import ue_capability_get, ue_context_get
from .facade_plan import ue_plan_validate
from .runtime import mcp
from .tools_facade import ue_diff_get, ue_execute, ue_read

__all__ = [
    "main",
    "ue_capability_get",
    "ue_context_get",
    "ue_diff_get",
    "ue_execute",
    "ue_plan_validate",
    "ue_read",
]


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
