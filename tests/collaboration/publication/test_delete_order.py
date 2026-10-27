"""Deletion checkpoints must never serialize a reference to an already deleted asset."""

from pathlib import Path

import pytest

from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_function_raw, prop
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.io import digest

CHILD = "/Game/Z_CDF.Z_CDF"
PARENT = "/Game/A_Coverage.A_Coverage"
STEP = "/Game/M_Step.M_Step"
CALL_CLASS = "/Script/Engine.MaterialExpressionMaterialFunctionCall"


class CheckpointUe(ProtocolUe):
    def call(self, operation, payload, **kwargs):
        if payload.get("delete_asset") and payload.get("apply_id"):
            if getattr(self, "fail_checkpoint", None) == payload["asset_path"]:
                request = dict(payload, operation=operation)
                receipt = dict(apply_id=payload["apply_id"], request=request, request_digest=digest(request), phase="rejected")
                self.receipts[payload["apply_id"]] = receipt
                return dict(ok=False, error=dict(code="checkpoint_failed", message="checkpoint serialization refused"),
                            data=dict(receipt=receipt))
            raw = self.assets[payload["asset_path"]]
            for node in raw["graph"]["nodes"]:
                for row in node["props"]:
                    if row["name"] == "MaterialFunction" and row["value"] not in self.assets:
                        return dict(ok=False, error=dict(code="checkpoint_failed", message="Illegal reference to private object"))
        return super().call(operation, payload, **kwargs)

    __call__ = call


@pytest.fixture
def project(tmp_path, request):
    ue = CheckpointUe()
    mode = getattr(request, "param", "default")
    for asset, dependency in ((CHILD, None), (PARENT, CHILD), (STEP, PARENT)):
        if mode == "cycle" and asset == CHILD:
            dependency = PARENT
        raw = material_function_raw()
        raw["asset_path"] = asset
        if dependency:
            raw["graph"]["nodes"].append(dict(guid="CALL-" + asset, name="Call", class_short="MaterialFunctionCall",
                x=0, y=0, inputs=["A", "B"], outputs=["Result"],
                props=[prop("MaterialFunction", "UMaterialFunctionInterface*", dependency, "None")],
                **dict(class_=CALL_CLASS)))
            raw["graph"]["nodes"][-1]["class"] = raw["graph"]["nodes"][-1].pop("class_")
        ue.assets[asset] = raw
    root = tmp_path / "mirror"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    workspace = run_sync(ue, "checkout", [CHILD, PARENT, STEP], dict(dry_run=False), env=env)
    record = dict(path=CALL_CLASS, inputs=["A", "B"], outputs=["Result"], dynamic_pins=True,
                  props=dict(MaterialFunction=dict(type="object", kind="object", default="None")))
    publish(root / ".nexus/schema" / SCHEMA_KEY, SCHEMA_KEY, dict(material_expression=dict(((CALL_CLASS, record),))), incremental=True)
    step = Path(workspace["file_paths"][STEP])
    if mode != "external":
        step.write_text("\n".join(line for line in step.read_text(encoding="utf-8").splitlines()
                                  if "MaterialFunctionCall(" not in line) + "\n", encoding="utf-8")
    for asset in (CHILD, PARENT):
        Path(workspace["file_paths"][asset]).unlink()
    options = dict(workspace_id=workspace["id"], dry_run=False, allow_delete=True)
    run_sync(ue, "commit", options=dict(options, all=True, delete=True, message="restore Step and remove experiments"), env=env)
    return ue, env, options


@pytest.mark.parametrize("paths", [[CHILD, PARENT, STEP], [STEP, CHILD, PARENT], [PARENT, STEP, CHILD]])
def test_live_dependency_is_deleted_after_its_referrer(project, paths):
    ue, env, options = project
    result = run_sync(ue, "push", paths, options, env=env)
    assert result["status"] == "published", result
    assert [row["asset_path"] for row in ue.applied] == [STEP, PARENT, CHILD]
    assert CHILD not in ue.assets and PARENT not in ue.assets and STEP in ue.assets
    assert len(ue.receipts) == 3


@pytest.mark.parametrize("failed", [STEP, PARENT])
def test_failed_referrer_blocks_the_dependency_even_when_continuing(project, failed):
    ue, env, options = project
    dirty = next(asset for asset in ue.assets if asset not in (CHILD, PARENT, STEP))
    ue.dirty.add(dirty)
    ue.fail_save = set((failed,))
    result = run_sync(ue, "push", [CHILD, PARENT, STEP], dict(options, stop_on_error=False), env=env)
    assert result["status"] in ("blocked", "partial"), result
    assert CHILD in ue.assets
    assert PARENT in ue.assets
    assert not any(row.get("delete_asset") and row["asset_path"] == CHILD for row in ue.applied)
    assert dirty in ue.dirty
    assert any(row["phase"] == "rolled_back" for row in ue.receipts.values())


@pytest.mark.parametrize("project", ["cycle"], indirect=True)
def test_deletion_cycle_is_rejected_before_any_asset_changes(project):
    ue, env, options = project
    with pytest.raises(SyncError) as caught:
        run_sync(ue, "push", [CHILD, PARENT, STEP], options, env=env)
    assert caught.value.code == "dependency_cycle"
    assert set(caught.value.details["assets"]) == set((CHILD, PARENT))
    assert not ue.applied and not ue.receipts


@pytest.mark.parametrize("project", ["external"], indirect=True)
def test_surviving_referrer_blocks_its_deleted_dependencies(project):
    ue, env, options = project
    result = run_sync(ue, "push", [CHILD, PARENT, STEP], options, env=env)
    assert result["status"] == "blocked", result
    assert CHILD in ue.assets and PARENT in ue.assets and STEP in ue.assets
    assert not ue.applied and not ue.receipts


def test_partial_receipt_names_committed_deletion_and_checkpoint_rejection(project):
    ue, env, options = project
    ue.fail_checkpoint = CHILD
    dirty = next(asset for asset in ue.assets if asset not in (CHILD, PARENT, STEP))
    ue.dirty.add(dirty)
    result = run_sync(ue, "push", [CHILD, PARENT, STEP], options, env=env)
    assert result["status"] == "partial", result
    rows = result["rows"]
    assert any(row.get("asset") == PARENT and row.get("deleted") is True for row in rows)
    assert any(row.get("asset") == CHILD and row.get("code") == "checkpoint_failed" for row in rows)
    assert PARENT not in ue.assets and CHILD in ue.assets
    assert dirty in ue.dirty
