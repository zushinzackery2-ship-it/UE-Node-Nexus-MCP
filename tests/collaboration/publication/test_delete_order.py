"""Deletion checkpoints must never serialize a reference to an already deleted asset."""

from pathlib import Path

import pytest

from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_function_raw, prop
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.sync.service import run_sync

CHILD = "/Game/A_CDF.A_CDF"
PARENT = "/Game/B_Coverage.B_Coverage"
STEP = "/Game/Z_Step.Z_Step"
CALL_CLASS = "/Script/Engine.MaterialExpressionMaterialFunctionCall"


class CheckpointUe(ProtocolUe):
    def call(self, operation, payload, **kwargs):
        if payload.get("delete_asset") and payload.get("apply_id"):
            raw = self.assets[payload["asset_path"]]
            for node in raw["graph"]["nodes"]:
                for row in node["props"]:
                    if row["name"] == "MaterialFunction" and row["value"] not in self.assets:
                        return dict(ok=False, error=dict(code="checkpoint_failed", message="Illegal reference to private object"))
        return super().call(operation, payload, **kwargs)

    __call__ = call


@pytest.fixture
def project(tmp_path):
    ue = CheckpointUe()
    for asset, dependency in ((CHILD, None), (PARENT, CHILD), (STEP, PARENT)):
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
    ue.fail_save = set((failed,))
    result = run_sync(ue, "push", [CHILD, PARENT, STEP], dict(options, stop_on_error=False), env=env)
    assert result["status"] in ("blocked", "partial"), result
    assert CHILD in ue.assets
    assert PARENT in ue.assets
    assert not any(row.get("delete_asset") and row["asset_path"] == CHILD for row in ue.applied)
