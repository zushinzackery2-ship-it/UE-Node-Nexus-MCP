"""Run Issues2 acceptance with staged or installed plugins in a disposable project."""

import json
import os
import argparse
from pathlib import Path
from uuid import uuid4

from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession
from tests.live.editor.windows import verify_visible
from .workflow import Workflow
from .acceptance import delete_asset, published_receipt_is_protected, stub_stability
from .readback import reject_creation


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path)
    parser.add_argument("--engine-plugins", action="store_true", help="Verify the installed engine plugin copies")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    build = root / "build/validation"
    engine = Path(os.environ["UE_NEXUS_ENGINE_DIR"])
    plugin_root = engine / "Engine/Plugins/Editor" if args.engine_plugins else None
    if args.project:
        project = args.project.resolve(strict=True)
        project.relative_to(root / "build/issues2-live")
        host = project.parent
    else:
        host = root / "build/issues2-live" / uuid4().hex[:12]
        host.mkdir(parents=True)
        (host / "Content").mkdir()
        if not args.engine_plugins:
            stage_plugins(build, host)
        project = host / "NexusValidation.uproject"
        project.write_bytes((build / project.name).read_bytes())
        runtime = host / "Runtime"
        runtime.mkdir()
        # This empty acceptance project has its own resource policy and process cap.
        (runtime / "policy.json").write_text(json.dumps(dict(min_free_gib=1, min_free_ratio=0.03)), encoding="utf-8")
    with EditorSession(project, engine, "Issues2") as session:
        verify_visible(session)
        session.verify_builds(plugin_root)
        workflow = Workflow(session)
        workflow.prepare()
        workflow.recovery()
        workflow.blueprints()
        workflow.reject_readback()
        reject_creation(workflow)
        published_receipt_is_protected(workflow)
        delete_asset(workflow)
        stub_stability(workflow)
    with EditorSession(project, engine, "Issues2Cold") as session:
        verify_visible(session)
        session.verify_builds(plugin_root)
        Workflow(session).cold()
    print(json.dumps(dict(ok=True, host=str(host), cold_restart=True)), flush=True)


if __name__ == "__main__":
    main()
