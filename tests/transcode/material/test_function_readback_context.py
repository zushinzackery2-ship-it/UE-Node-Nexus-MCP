"""Saved single-output function calls use the same candidate signature as their requests."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.material.interfaces import documents
from ue_node_nexus_mcp.transcode.raw.common import RAW_VERSION
from ue_node_nexus_mcp.transcode.text.parser import parse


PROVIDER = (
    "nexus: 1\nasset: /Game/MF_A\nclass: MaterialFunction\n"
    "[graph]\nresult : FunctionOutput(OutputName=Offset)\n"
)
CONSUMER = (
    "nexus: 1\nasset: /Game/M_A\nclass: Material\n[graph]\n"
    "call : MaterialFunctionCall(MaterialFunction=/Game/MF_A.MF_A)\n"
    "wrong : Constant(R=0.5)\ncall -> out.EmissiveColor\n"
)
CALL_GUID = "11111111-1111-1111-1111-111111111111"
WRONG_GUID = "22222222-2222-2222-2222-222222222222"


def fixture(tmp_path, wrong=False):
    provider, _ = parse(PROVIDER)
    saved_provider = capture(PROVIDER, None, "provider", "material_function")
    with documents([("material_function", provider)]):
        requested = capture(CONSUMER, None, "request", "material")
    store = Store(tmp_path / "repository")
    entries = dict()
    entries["/Game/MF_A.MF_A"] = store.objects.put("snapshot", saved_provider)
    entries["/Game/M_A.M_A"] = store.objects.put("snapshot", requested)
    workspace = SimpleNamespace(store=store, history=SimpleNamespace(entries=lambda revision: entries),
        schema=None, root=Path(tmp_path), state=dict(files=dict()))
    nodes = [
        dict(guid=CALL_GUID, class_short="MaterialFunctionCall", inputs=[], outputs=[""],
             props=[dict(name="MaterialFunction", type="object", value="/Game/MF_A.MF_A", default="None")]),
        dict(guid=WRONG_GUID, class_short="Constant", inputs=[], outputs=[""],
             props=[dict(name="R", type="float", value="0.5", default="0")])
    ]
    for node in nodes:
        node["class"] = "/Script/Engine.MaterialExpression" + node["class_short"]
    raw = dict(raw_version=RAW_VERSION, kind="material", asset_path="/Game/M_A.M_A", class_short="Material", schema_key="",
        graph=dict(nodes=nodes, links=[], outputs=[dict(
            property="EmissiveColor", **dict([( "from", WRONG_GUID if wrong else CALL_GUID)]), from_out=0)]))
    plan = [dict(op="connect_pins", to="out", to_pin="EmissiveColor", **dict([("from", "call")]))]
    record = dict(id="readback", candidate="candidate", asset="/Game/M_A.M_A",
        request=dict(plan=plan), receipt=dict(after=raw, response_data=dict(id_map=dict(call=CALL_GUID, wrong=WRONG_GUID))))
    return workspace, record


def test_single_output_readback_uses_candidate_interface(tmp_path):
    workspace, record = fixture(tmp_path)
    assert actual_snapshot(workspace, record) is not None


def test_candidate_interface_keeps_real_miswire_rejected(tmp_path):
    workspace, record = fixture(tmp_path, wrong=True)
    with pytest.raises(SyncError, match="readback differs"):
        actual_snapshot(workspace, record)
