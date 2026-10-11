"""Exercise requested property writes through the actual publication receipt."""

from copy import deepcopy

import pytest

from tests.support.semantics import receipt_workspace
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.errors import SyncError


def receipt(tmp_path, expected, actual, contract):
    raw = dict(raw_version=1, asset_path="/Game/BP_Test.BP_Test", kind="blueprint",
               schema_key="key", class_short="Blueprint", props=[],
               blueprint=dict(defaults=[dict(name="Settings", type=contract["type"],
                                            value=expected, value_schema=contract)], graphs=[]))
    workspace, commit, asset = receipt_workspace(tmp_path, from_raw(raw))
    after = deepcopy(raw)
    after["blueprint"]["defaults"][0]["value"] = actual
    record = dict(id="apply", asset=asset, candidate=commit,
                  request=dict(plan=[dict(op="bp_default_set", name="Settings", value=expected)]),
                  receipt=dict(after=after))
    return workspace, record


SETTINGS = dict(kind="struct", type="Settings", fields=dict(
    Enabled=dict(type="bool"), Width=dict(type="int"),
    Nested=dict(kind="struct", type="Nested", fields=dict(X=dict(type="float"), Y=dict(type="float")))))


@pytest.mark.parametrize(("expected", "actual", "contract"), [
    ("(Enabled=true)", "(Enabled=True,Width=640)", SETTINGS),
    ("(Nested=(X=1.5))", "(Width=640,Nested=(Y=9,X=1.500000))", SETTINGS),
    ("((Enabled=true),(Width=10))", "((Enabled=True,Width=4),(Enabled=False,Width=10))",
     dict(kind="array", type="TArray<Settings>", element=SETTINGS)),
    ("true", "True", dict(type="uint8")),
    ("false", "0", dict(type="uint8")),
])
def test_requested_fields_accept_native_expansion(tmp_path, expected, actual, contract):
    workspace, record = receipt(tmp_path, expected, actual, contract)
    assert actual_snapshot(workspace, record)["raw"]["blueprint"]["defaults"][0]["value"] == actual


@pytest.mark.parametrize(("expected", "actual", "contract"), [
    ("(Enabled=true)", "(Enabled=False,Width=640)", SETTINGS),
    ("(Nested=(X=1.5))", "(Nested=(X=2,Y=9))", SETTINGS),
    ("((Width=1),(Width=2))", "((Width=2),(Width=1))",
     dict(kind="array", type="TArray<Settings>", element=SETTINGS)),
    ("((Width=1))", "((Width=1),(Width=1))",
     dict(kind="array", type="TArray<Settings>", element=SETTINGS)),
    ("true", "false", dict(type="uint8")),
    ("true", "True", dict(type="FString")),
])
def test_written_fields_and_array_shape_remain_strict(tmp_path, expected, actual, contract):
    workspace, record = receipt(tmp_path, expected, actual, contract)
    with pytest.raises(SyncError, match="readback differs"):
        actual_snapshot(workspace, record)
