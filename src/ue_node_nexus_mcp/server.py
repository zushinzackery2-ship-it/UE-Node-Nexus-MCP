from __future__ import annotations

from .facade_capabilities import ue_capability_get, ue_context_get
from .facade_plan import ue_plan_validate
from .runtime import consume_cli_arguments, mcp
from .tools_facade import ue_diff_get, ue_execute, ue_read
from .tools_sync import ue_sync
from .diagnostics.logging import configure_logging

__all__ = [
    "main",
    "ue_capability_get",
    "ue_context_get",
    "ue_diff_get",
    "ue_execute",
    "ue_plan_validate",
    "ue_read",
    "ue_sync",
]


def main() -> None:
    configure_logging()
    consume_cli_arguments()
    mcp.run()


if __name__ == "__main__":
    main()
