"""Run DX12 scene acceptance against an explicitly prepared project copy."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from tests.live.editor.session import EditorSession
from tests.live.scene.roundtrip import exercise, verify_reopened


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--map", default="/Game/WaterStains/Level")
    args = parser.parse_args()
    project = args.host.resolve() / "NexusValidation.uproject"
    engine = Path(os.environ["UE_NEXUS_ENGINE_DIR"])
    with EditorSession(project, engine, "SceneAcceptance") as session:
        session.verify_builds()
        result = exercise(session, args.map)
    with EditorSession(project, engine, "SceneReopen") as session:
        session.verify_builds()
        verify_reopened(session, result)
    print(json.dumps(dict(ok=True, scene=result, reopened=True)))


if __name__ == "__main__":
    main()
