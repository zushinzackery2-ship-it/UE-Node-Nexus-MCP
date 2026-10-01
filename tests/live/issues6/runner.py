"""Run Issues6 in an isolated project with its own plugins and manager state."""

import json
from pathlib import Path
from uuid import uuid4

from tests.live.editor.prepare import stage_plugins
from tests.live.editor.session import EditorSession
from .workflow import exercise


def main():
    root = Path(__file__).resolve().parents[3]
    build = root / "build/validation"
    host = root / "build/issues6/live" / uuid4().hex[:12]
    host.mkdir(parents=True)
    (host / "Content").mkdir()
    stage_plugins(build, host)
    project = host / "NexusValidation.uproject"
    project.write_bytes((build / project.name).read_bytes())
    runtime = host / "Runtime"
    runtime.mkdir()
    (runtime / "policy.json").write_text(json.dumps(dict(min_free_gib=1, min_free_ratio=0.03)), encoding="utf-8")
    result = dict(host=str(host), ok=False)
    try:
        with EditorSession(project, Path("D:/Program Files/Epic Games/UE_5.5"), "Issues6") as session:
            result["build"] = session.verify_builds()["build"]
            result["workflow"] = exercise(session)
        result["ok"] = True
    finally:
        (host / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(dict(host=str(host), ok=result["ok"])), flush=True)


if __name__ == "__main__":
    main()
