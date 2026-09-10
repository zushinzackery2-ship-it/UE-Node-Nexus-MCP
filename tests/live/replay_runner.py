"""Replay the original Forge batch, render presets and reopen the saved project."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from tests.live.editor.session import EditorSession
from tests.live.replay.batch import exercise, verify_reopened
from tests.live.replay.render import capture, exercise as render


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--map", default="/Game/WaterStains/Level")
    parser.add_argument("--phase", choices=("all", "render", "reopen"), default="all")
    args = parser.parse_args()
    project = args.host.resolve() / "NexusValidation.uproject"
    engine = Path(os.environ["UE_NEXUS_ENGINE_DIR"])
    previous_build = None
    if args.phase != "all":
        batch = json.loads((project.parent / "Logs/ForgeReplay-replay-result.json").read_text(encoding="utf-8"))
        previous_build = json.loads((project.parent / "Logs/ForgeReplay-builds.json").read_text(encoding="utf-8"))["build"]
    if args.phase != "reopen":
        with EditorSession(project, engine, "ForgeReplay") as session:
            current = session.verify_builds()["build"]
            if previous_build is not None:
                assert all(current[name]["source_fingerprint"] == row["source_fingerprint"] for name, row in previous_build.items())
            else:
                batch = exercise(session)
            previous_build = current
            renders = render(session, batch["instances"], args.map)
    else:
        renders = json.loads((project.parent / "Logs/ForgeReplay-renders.json").read_text(encoding="utf-8"))
    with EditorSession(project, engine, "ForgeReopen") as session:
        current = session.verify_builds()["build"]
        assert all(current[name]["source_fingerprint"] == row["source_fingerprint"] for name, row in previous_build.items())
        verify_reopened(session, batch)
        session.report("render-reopened", capture(session, "Reopened"))
    print(json.dumps(dict(ok=True, assets=len(batch["assets"]), rounds=batch["rounds"],
                          rendered_presets=len(renders["presets"]), reopened=True)))


if __name__ == "__main__":
    main()
