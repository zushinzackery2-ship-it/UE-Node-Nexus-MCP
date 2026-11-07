"""Drive generated text through real capture, commit, publication and cold reload."""

import json
from pathlib import Path
import time

from ue_node_nexus_mcp.transcode.sync.service import run_sync
from .sequences import documents, edit_existing


class Campaign:
    def __init__(self, session, directory, actions, seed, state=None):
        self.session = session
        self.directory = Path(directory)
        self.actions = actions
        self.seed = seed
        self.state = state or {}
        self.env = dict(UE_NEXUS_TRANSCODE_DIR=self.state.get("mirror", str(self.directory.parent / "Mirror")))
        self.rows = []

    def call(self, action, paths=None, **options):
        options.setdefault("dry_run", False)
        if self.state.get("workspace"):
            options.setdefault("workspace_id", self.state["workspace"])
        start = time.monotonic()
        result = run_sync(self.session.call, action, paths, options, self.env)
        self.rows.append(dict(action=action, options=options, seconds=time.monotonic() - start, result=result))
        self.write("workflow.json", self.rows)
        if result.get("error_count") or result.get("errors") or result.get("status") in ("partial", "conflict", "failed"):
            raise AssertionError(json.dumps(dict(action=action, result=result), ensure_ascii=False))
        return result

    def write(self, name, value):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def initialize(self):
        root = f"/Game/NexusStress_{self.seed}"
        checkout = self.call("checkout", agent_id="stress")
        self.state.update(workspace=checkout["id"], files_root=checkout["files_root"],
                          schema=checkout["schema_key"], root=root, mirror=self.env["UE_NEXUS_TRANSCODE_DIR"])
        self.write("state.json", self.state)

    def execute(self):
        if not self.state:
            self.initialize()
        for action in self.actions:
            self.write("current-action.json", action)
            authored = documents(self.state["root"], self.state["schema"], action)
            for name, text in authored.items():
                path = Path(self.state["files_root"]) / self.state["root"].removeprefix("/Game/") / name
                path.parent.mkdir(parents=True, exist_ok=True)
                if path.is_file():
                    text = edit_existing(name, path.read_text(encoding="utf-8"), action)
                path.write_text(text, encoding="utf-8")
            self.call("lint")
            self.call("commit", all=True, message=f"stress seed {self.seed} action {action['index']}")
            self.call("push", allow_delete=True, isolation="off")
            stable = self.call("push", allow_delete=True, isolation="off")
            assert stable.get("applied") == 0, stable
        self.write("state.json", self.state)
        return self.state

    def reopened(self):
        observed = self.call("fetch", paths=[self.state["root"]])
        stable = self.call("push", allow_delete=True, isolation="off")
        assert stable.get("applied") == 0, stable
        return dict(fetch=observed, stable=stable)
