"""Exercise real create, edit, compile, receipt and cold-start paths."""

from copy import deepcopy
from pathlib import Path
from shutil import copy2
from time import monotonic

from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.instances.live.protection import save_asset

FIXTURES = Path(__file__).parent / "fixtures"
PREFIX = "/Game/NexusIssues2/"


class Workflow:
    def __init__(self, session):
        self.session = session
        mapping = session.session.repository()
        self.env = dict(UE_NEXUS_TRANSCODE_DIR=mapping["mirror_root"])
        self.repository = mapping["repository"]
        self.results = []
        self.workspace = None
        self.bridge = session.call

    def call(self, action, paths=None, **options):
        options.setdefault("dry_run", False)
        if self.workspace:
            options["workspace_id"] = self.workspace["id"]
        started = monotonic()
        try:
            result = run_sync(self.bridge, action, paths, options, self.env)
        except SyncError as error:
            self.results.append(dict(action=action, elapsed=monotonic() - started,
                                     error=dict(code=error.code, message=str(error), details=error.details)))
            self.session.report("workflow", self.results)
            raise
        self.results.append(dict(action=action, elapsed=monotonic() - started, result=result))
        self.session.report("workflow", self.results)
        print(action + ": " + str(result.get("status", result.get("action", "ok"))), flush=True)
        return result

    def file(self, name):
        return Path(self.workspace["files_root"]) / "NexusIssues2" / name

    def published(self, result):
        errors = dict((asset, error.get("code")) for asset, error in result.get("errors", dict()).items())
        assert result.get("status") == "published", (result.get("status"), errors, str(self.session.logs))
        return result

    def introduce(self, *names):
        for name in names:
            target = self.file(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            copy2(FIXTURES / name, target)
        self.call("commit", all=True, message="Introduce " + ", ".join(names))
        return self.call("push")

    def edit(self, name, old, new):
        file = self.file(name)
        text = file.read_text(encoding="utf-8")
        assert old in text, (old, text)
        file.write_text(text.replace(old, new), encoding="utf-8")
        self.call("commit", all=True, message="Edit " + name)
        result = self.call("push", allow_delete=True)
        self.published(result)
        return result

    def compile(self, name):
        data = self.session.require("asset_compile", asset_path=PREFIX + name + "." + name)
        self.session.report("compile-" + name, data)
        assert not any(row.get("severity") == "error" for row in data.get("diagnostics", [])), data
        checks = data.get("post_checks", dict()).get("compile", dict())
        assert checks.get("ok", True), data
        save_asset(self.session, PREFIX + name + "." + name)
        return data

    def prepare(self):
        self.workspace = self.call("checkout", paths=[], agent_id="issues2-validation")
        result = self.introduce("MF_Precision.mf.nexus", "M_State.mat.nexus", "MI_State.mi.nexus")
        self.published(result)
        text = self.file("MF_Precision.mf.nexus").read_text(encoding="utf-8")
        assert "ComponentMask(B=true)" in text or "ComponentMask(B=True)" in text, text
        assert "ConstB=0.000000000001" in text, text
        for name in ("MF_Precision", "M_State", "MI_State"):
            self.compile(name)
        self.edit("MI_State.mi.nexus", "Amount = 0.338", "Amount = 0.7")
        self.edit("M_State.mat.nexus", "DefaultValue=0.338", "DefaultValue=0.7")
        assert self.call("push")["applied"] == 0

    def blueprints(self):
        result = self.introduce("BPC_Cold.bp.nexus", "BPI_Query.bp.nexus")
        self.published(result)
        self.call("schema", refresh=True)
        result = self.introduce("BP_Main.bp.nexus")
        self.published(result)
        self.compile("BP_Main")
        text = self.file("BP_Main.bp.nexus").read_text(encoding="utf-8")
        assert "[interfaces]" in text and "BPI_Query_C" in text, text
        assert "Changed(Value: float)" in text and "VariableGet(cache)" in text, text
        self.edit("BP_Main.bp.nexus", "Changed(Value: float)", "Changed(Value: float, Label: string)")
        self.compile("BP_Main")
        assert self.call("push")["applied"] == 0

        from .acceptance import blueprint_edits

        blueprint_edits(self)

    def recovery(self):
        failed = self.introduce("MF_Retry.mf.nexus")
        assert failed["error_count"] and failed["applied"] == 0, failed
        error = next(iter(failed["errors"].values()))
        assert error["details"].get("phase") == "rolled_back", failed
        assert not (self.session.project.parent / "Content/NexusIssues2/MF_Retry.uasset").exists()
        self.edit("MF_Retry.mf.nexus", "return missing_identifier;", "return 0.5;")
        self.compile("MF_Retry")

    def reject_readback(self):
        file = self.file("M_State.mat.nexus")
        text = file.read_text(encoding="utf-8")
        assert "DefaultValue=0.7" in text, text
        file.write_text(text.replace("DefaultValue=0.7", "DefaultValue=0.8"), encoding="utf-8")
        self.call("commit", all=True, message="Verify rejected receipt restoration")

        def altered(operation, payload):
            response = self.session.call(operation, payload)
            if operation == "transcode_apply" and response.get("ok"):
                response = deepcopy(response)
                receipt = response["data"]["receipt"]
                node = next(node for node in receipt["after"]["graph"]["nodes"] if node["class_short"] == "ScalarParameter")
                next(prop for prop in node["props"] if prop["name"] == "DefaultValue")["value"] = "999"
            return response

        self.bridge = altered
        try:
            result = self.call("push")
        finally:
            self.bridge = self.session.call
        assert result["applied"] == 0 and result["error_count"] == 1, result
        error = next(iter(result["errors"].values()))
        assert error["code"] == "apply_result_mismatch" and error["details"]["phase"] == "rolled_back", result
        assert self.call("push")["status"] == "published"

    def cold(self):
        self.workspace = self.call("checkout", paths=[PREFIX], agent_id="issues2-cold")
        result = self.call("schema", refresh=True, category="component", query="BPC_Cold", details=True)
        assert result["total"] == 1, result
        self.compile("MI_State")
        text = self.file("MI_State.mi.nexus").read_text(encoding="utf-8")
        old = next(line for line in text.splitlines() if line.startswith("UseHigh = "))
        self.edit("MI_State.mi.nexus", old, "UseHigh = False")
        self.compile("MI_State")
        assert self.call("lint")["error_count"] == 0
        assert self.call("push")["applied"] == 0

        from .acceptance import stub_stability

        stub_stability(self, cold=True)
