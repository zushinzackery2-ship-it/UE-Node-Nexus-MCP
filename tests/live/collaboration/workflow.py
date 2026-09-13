"""Two independent workspaces publish against the real editor protocol."""

from __future__ import annotations

from pathlib import Path
import stat
from uuid import uuid4

from ue_node_nexus_mcp.transcode.paths import text_path
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.collaboration.store.repository import Store


class Workflow:
    def __init__(self, session) -> None:
        self.session = session
        self.root = session.project.parent / "CollaborationMirror" / uuid4().hex
        self.env = dict(UE_NEXUS_TRANSCODE_DIR=str(self.root))
        self.asset = "/Game/Collaboration_" + uuid4().hex[:8] + "/M_Main.M_Main"
        self.bridge = session.call
        self.results = []

    def call(self, action, workspace=None, paths=None, **options):
        options.setdefault("dry_run", False)
        if workspace:
            options["workspace_id"] = workspace["id"]
        result = run_sync(self.bridge, action, paths, options, self.env)
        self.results.append(dict(action=action, result=result))
        self.session.report("workflow", self.results)
        return result

    def edit(self, workspace, old, new) -> None:
        path = Path(workspace["file_paths"][self.asset])
        text = path.read_text(encoding="utf-8")
        assert old in text, (old, text)
        path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")

    def commit(self, workspace, message):
        return self.call("commit", workspace, all=True, message=message)

    def prepare(self):
        initialized = self.call("init", pull_all=False)
        project = self.root / self.session.project.stem
        path = text_path(project, self.asset, "material")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"nexus: 1\nasset: {self.asset}\nclass: Material\nschema: {initialized['schema_key']}\n\n"
                        "[asset]\nTwoSided = true\n\n[graph]\nc : Constant(R=0.25) @ 0,0\nc -> out.BaseColor\n", encoding="utf-8")
        created = self.call("push", paths=[self.asset])
        assert not created["error_count"], created
        first = self.call("checkout", paths=[self.asset], agent_id="A")
        second = self.call("checkout", paths=[self.asset], agent_id="B")
        self.store = Store(project / ".nexus/collaboration", str(self.session.project))
        return first, second

    def distinct_changes(self, first, second):
        self.edit(first, "R=0.25", "R=0.5")
        self.commit(first, "A: change value")
        published = self.call("push", first)
        assert published["status"] == "published", published
        self.edit(second, "TwoSided = True", "TwoSided = False")
        self.commit(second, "B: change sidedness from old copy")
        published = self.call("push", second)
        assert published["status"] == "published", published
        text = Path(second["file_paths"][self.asset]).read_text(encoding="utf-8")
        assert "R=0.5" in text and "TwoSided = True" not in text, text
        return published

    def conflict(self, first, second):
        self.call("pull", first)
        self.edit(first, "R=0.5", "R=0.6")
        self.edit(second, "R=0.5", "R=0.7")
        self.commit(first, "A: conflict")
        self.commit(second, "B: conflict")
        assert self.call("push", first)["status"] == "published"
        report = self.call("push", second)
        assert report["status"] == "conflict", report
        for item in report["conflicts"]:
            self.call("resolve", second, merge_id=report["merge_id"], conflict_id=item["conflict_id"], choice="ours")
        result = self.call("continue", second, merge_id=report["merge_id"])
        assert result["status"] == "published", result
        return result

    def lost_response(self, workspace):
        self.edit(workspace, "R=0.7", "R=0.8")
        self.commit(workspace, "receipt replay")
        lost = []

        def bridge(operation, payload):
            response = self.session.call(operation, payload)
            if operation == "transcode_apply" and payload.get("apply_id") and not lost:
                assert response.get("ok"), response
                lost.append(payload["apply_id"])
                raise OSError("deliberately discard the saved response")
            return response

        self.bridge = bridge
        try:
            result = self.call("push", workspace)
        finally:
            self.bridge = self.session.call
        assert result["status"] == "published", result
        assert lost
        record = self.store.record("apply", lost[0])
        replay = self.session.require(record["operation"], **record["request"])
        assert replay["receipt"]["apply_id"] == lost[0], replay
        assert self.call("push", workspace)["applied"] == 0
        return lost[0]

    def read_only(self, workspace):
        self.edit(workspace, "R=0.8", "R=0.9")
        self.commit(workspace, "read-only package")
        package = self.session.project.parent / "Content" / (self.asset.split(".", 1)[0].removeprefix("/Game/") + ".uasset")
        package.chmod(stat.S_IREAD)
        try:
            result = self.call("push", workspace)
            assert result["error_count"] and result["applied"] == 0, result
        finally:
            package.chmod(stat.S_IREAD | stat.S_IWRITE)
        retried = self.call("push", workspace)
        assert retried["status"] == "published", retried
        return result


def exercise(session) -> dict:
    workflow = Workflow(session)
    first, second = workflow.prepare()
    workflow.distinct_changes(first, second)
    workflow.conflict(first, second)
    apply_id = workflow.lost_response(second)
    workflow.read_only(second)
    return dict(mirror=str(workflow.root), asset=workflow.asset, apply_id=apply_id, workspace=second["id"],
                assertions=["isolated workspaces", "stale-copy merge", "conflict continuation", "saved receipt replay", "save preflight"])
