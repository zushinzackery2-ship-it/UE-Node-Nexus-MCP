"""Run collaboration smoke in a fresh, isolated UE project."""

from __future__ import annotations

import json
import os
from pathlib import Path
from uuid import uuid4

from tests.live.collaboration.workflow import exercise
from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("UE_NEXUS_VALIDATION_DIR", root / "build/validation"))
    host = root / "build/collaboration-live" / uuid4().hex[:12]
    host.mkdir(parents=True)
    stage_plugins(build, host)
    project = host / "NexusValidation.uproject"
    project.write_bytes((build / project.name).read_bytes())
    engine = Path(os.environ["UE_NEXUS_ENGINE_DIR"])
    with EditorSession(project, engine, "Collaboration") as session:
        session.verify_builds()
        result = exercise(session)
        session.report("result", result)
    print(json.dumps(dict(ok=True, **result)), flush=True)


if __name__ == "__main__":
    main()
