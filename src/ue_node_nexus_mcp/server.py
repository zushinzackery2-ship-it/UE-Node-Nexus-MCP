from __future__ import annotations

from .runtime import call_bridge as _call
from .runtime import mcp


def main() -> None:
    mcp.run()


from .tools_facade import ue_capability_get, ue_context_get, ue_diff_get, ue_execute, ue_plan_validate, ue_read  # noqa: E402,F401


if __name__ == "__main__":
    main()
