"""The legacy apply seam cannot bypass guarded high-risk publication."""

from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.push.apply import apply_item
from ue_node_nexus_mcp.transcode.push.model import PushOptions, PushResult
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.mark.parametrize("kind,plan,interface", [
    ("niagara_system", [dict(op="ns_module_add")], False),
    ("material", [dict(op="create_node", node_class="MaterialExpressionCustom")], False),
    ("material_function", [dict(op="create_node", node_class="MaterialExpressionFunctionInput")], True),
])
def test_legacy_high_risk_requires_guarded_workspace(tmp_path, kind, plan, interface):
    file = tmp_path / "Probe.nexus"
    file.write_text("candidate", encoding="utf-8")
    item = SimpleNamespace(file=file, text="candidate", status=SimpleNamespace(kind=kind, asset_path="/Game/Probe.Probe"),
        plan=SimpleNamespace(to_payload=lambda: dict(plan=plan), interface_changed=interface), document=parse("")[0])
    calls = []
    with pytest.raises(SyncError) as raised:
        apply_item(lambda *args: calls.append(args), None, None, item, PushOptions(), PushResult())
    assert raised.value.code == "safety_workspace_required"
    assert raised.value.details["next"]["action"] == "checkout"
    assert not calls
