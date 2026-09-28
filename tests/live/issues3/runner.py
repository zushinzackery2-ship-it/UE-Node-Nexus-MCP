"""Run the complete Issues3 mirror path in a fresh project copy."""

import argparse
import json
from pathlib import Path
from uuid import uuid4

from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession
from .workflow import exercise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-plugins", action="store_true", help="Validate plugins installed in the engine")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    build = root / "build/validation"
    host = root / "build/issues3-live" / uuid4().hex[:12]
    host.mkdir(parents=True)
    if not args.engine_plugins:
        stage_plugins(build, host)
    project = host / "NexusValidation.uproject"
    project.write_bytes((build / project.name).read_bytes())
    engine = Path("D:/Program Files/Epic Games/UE_5.5")
    with EditorSession(project, engine, "Issues3") as session:
        session.verify_builds(engine / "Engine/Plugins/Editor" if args.engine_plugins else None)
        result = exercise(session)
        session.report("result", result)
    print(json.dumps(dict(ok=True, host=str(host))), flush=True)


if __name__ == "__main__":
    main()
