"""Run the complete Issues4 acceptance in a fresh isolated project.

One managed editor carries the mirror scenarios (#2 sparse push, #3 function
interface batch) and the managed start; three external starts follow: hidden,
visible and answered, visible and closed while its prompt waits.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
from uuid import uuid4

from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession
from .startup import Launches
from .workflow import Issues4


def host_project(root: Path, engine_plugins: bool) -> Path:
    build = root / "build/validation"
    host = root / "build/issues4-live" / uuid4().hex[:12]
    host.mkdir(parents=True)
    (host / "Content").mkdir()
    if not engine_plugins:
        stage_plugins(build, host)
    fixture = build / "Plugins/UeNexusStartupFixture"
    assert (fixture / "Binaries/Win64/UnrealEditor-UeNexusStartupFixture.dll").is_file(), "build the startup acceptance fixture first"
    shutil.copytree(fixture, host / "Plugins/UeNexusStartupFixture", ignore=shutil.ignore_patterns("Intermediate", "*.pdb"))
    (host / "StartupAdvisory.fixture").write_text("startup acceptance\n", encoding="utf-8")
    project = host / "NexusValidation.uproject"
    project.write_bytes((build / project.name).read_bytes())
    runtime = host / "Runtime"
    runtime.mkdir()
    # This empty acceptance project has its own resource policy.
    (runtime / "policy.json").write_text(json.dumps(dict(min_free_gib=1, min_free_ratio=0.03)), encoding="utf-8")
    return project


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-plugins", action="store_true", help="Validate plugins installed in the engine")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    engine = Path(os.environ.get("UE_NEXUS_ENGINE_DIR", "D:/Program Files/Epic Games/UE_5.5"))
    project = host_project(root, args.engine_plugins)
    launches = Launches(project, engine)
    launches.logs.mkdir(exist_ok=True)
    result = dict(host=str(project.parent), short_of_space=sorted(launches.expected), ok=False)
    try:
        with EditorSession(project, engine, "Issues4") as session:
            result["builds"] = session.verify_builds(engine / "Engine/Plugins/Marketplace" if args.engine_plugins else None)["build"]
            result["managed"] = launches.managed(session)
            issues = Issues4(session)
            result["fixtures"] = issues.create()
            result["sparse_push"] = issues.sparse_push()
            result["interface_batch"] = issues.interface_batch()
        result["hidden"] = launches.hidden()
        result["answered"] = launches.answered()
        result["exited_while_waiting"] = launches.exited_while_waiting()
        result["ok"] = True
    finally:
        (launches.logs / "Issues4-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(ok=True, host=str(project.parent)), ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
