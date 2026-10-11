"""Native property aliases and subobject names carry the same verified CDO identity."""

from copy import deepcopy

import pytest

from tests.support.semantics import receipt_workspace
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw, text_of
from ue_node_nexus_mcp.transcode.errors import SyncError


def native_raw(name="CollisionComponent", parent="CollisionComponent"):
    return dict(raw_version=1, asset_path="/Game/BP_Native.BP_Native", kind="blueprint",
                schema_key="key", class_short="Blueprint", props=[], blueprint=dict(graphs=[], components=[
                    dict(name=name, class_short="SphereComponent", **{"class": "/Script/Engine.SphereComponent"},
                         inherited=True, props=[], native_aliases=["CollisionComponent0", "CollisionComponent"]),
                    dict(name="View", class_short="CameraComponent", **{"class": "/Script/Engine.CameraComponent"},
                         parent=parent, inherited=False, props=[])]))


def publication(tmp_path, expected, actual):
    workspace, commit, asset = receipt_workspace(tmp_path, from_raw(expected))
    record = dict(id="apply", asset=asset, candidate=commit, request=dict(plan=[
        dict(op="bp_component_bind", name=expected["blueprint"]["components"][0]["name"], **{"class": "SphereComponent"}),
        dict(op="bp_component_add", name="View", **{"class": "CameraComponent"}, parent=expected["blueprint"]["components"][1]["parent"])]),
        receipt=dict(after=actual))
    return workspace, record


@pytest.mark.parametrize("history_name", ["CollisionComponent", "CollisionComponent0"])
def test_history_and_author_aliases_survive_actual_receipt(tmp_path, history_name):
    expected = native_raw(history_name, history_name)
    actual = native_raw("CollisionComponent0", "CollisionComponent0")
    workspace, record = publication(tmp_path, expected, actual)
    result = actual_snapshot(workspace, record)
    text = " ".join(text_of(result).split())
    assert f"{history_name} : @inherited(SphereComponent)" in text
    assert f"View : CameraComponent(parent={history_name})" in text
    assert from_raw(actual, result)["semantic_hash"] == result["semantic_hash"]


@pytest.mark.parametrize("changed", ["identity", "class", "parent"])
def test_inherited_identity_class_and_attachment_stay_strict(tmp_path, changed):
    expected = native_raw()
    actual = native_raw("CollisionComponent0", "CollisionComponent0")
    if changed == "identity":
        actual["blueprint"]["components"][0]["native_aliases"] = ["OtherComponent"]
    elif changed == "class":
        actual["blueprint"]["components"][0].update(class_short="CameraComponent", **{"class": "/Script/Engine.CameraComponent"})
    else:
        actual["blueprint"]["components"][1]["parent"] = "OtherComponent"
    workspace, record = publication(tmp_path, expected, actual)
    with pytest.raises(SyncError, match="readback differs"):
        actual_snapshot(workspace, record)


def test_old_object_name_history_does_not_need_new_alias_metadata(tmp_path):
    expected = native_raw("CollisionComponent0", "CollisionComponent0")
    actual = deepcopy(expected)
    actual["blueprint"]["components"][0].pop("native_aliases")
    workspace, record = publication(tmp_path, expected, actual)
    assert actual_snapshot(workspace, record)["semantic_hash"] == from_raw(expected)["semantic_hash"]
