"""One independently named scene regression and its accepted mirror state."""

from __future__ import annotations

import json
from pathlib import Path
import uuid

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.scene.paths import identity, scene_path, storage
from ue_node_nexus_mcp.transcode.sync import run_sync


class SceneWorkspace:
    def __init__(self, session, map_path: str, name: str | None = None) -> None:
        self.session = session
        self.map_path = map_path
        self.name = name or "Acceptance_" + uuid.uuid4().hex[:8]
        self.root = session.project.parent / "AcceptanceMirror"
        self.env = dict(UE_NEXUS_TRANSCODE_DIR=str(self.root))
        initialized = run_sync(session.call, "init", options=dict(pull_all=False), env=self.env)
        self.schema = initialized["schema_key"]
        self.project = self.root / session.project.stem
        self.file = scene_path(self.project, map_path, self.name)
        self.base = storage(self.project, identity(map_path, self.name), "base")

    def sync(self, action: str, **options) -> dict:
        report = run_sync(self.session.call, action, [str(self.file)], options, env=self.env)
        self.session.report(f"scene-{action}-{uuid.uuid4().hex[:8]}", report)
        return report

    def push(self, **options) -> dict:
        report = self.sync("push", dry_run=False, **options)
        assert report.get("error_count", 0) == 0 and report.get("scene_error_count", 0) == 0, report
        assert not report.get("scene_counts", dict()).get("failed"), report
        return report

    def document(self):
        document, diagnostics = parse(self.file.read_text(encoding="utf-8"))
        assert not diagnostics.has_errors, diagnostics
        return document

    def write(self, document) -> None:
        self.file.parent.mkdir(parents=True, exist_ok=True)
        self.file.write_text(emit(document), encoding="utf-8")

    def snapshot(self) -> dict:
        accepted = json.loads(self.base.read_text(encoding="utf-8"))
        output = self.project / ".nexus/scenes/acceptance-live.json"
        self.session.require("scene_export", map_path=self.map_path, name=self.name,
                             actors=[dict(id=actor["id"], level_path=actor["level_path"]) for actor in accepted["actors"]],
                             out_file=str(output))
        return json.loads(output.read_text(encoding="utf-8"))

    def summary(self) -> dict:
        snapshot = self.snapshot()
        return dict(map_path=self.map_path, name=self.name, file=str(self.file),
                    actors=sorted(actor["id"] for actor in snapshot["actors"]),
                    instances=dict((actor["id"] + "/" + component["id"], component["instance_data"]["instances"])
                                   for actor in snapshot["actors"] for component in actor["components"]
                                   if "instance_data" in component))
