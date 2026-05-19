from __future__ import annotations

import os
import sys
from pathlib import Path


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    src_path = repo_root / "src"
    sys.path.insert(0, str(src_path))
    os.environ.setdefault("UE_NEXUS_BRIDGE_URL", "http://127.0.0.1:8765")

    from ue_node_nexus_mcp.server import main as server_main

    server_main()


if __name__ == "__main__":
    main()
