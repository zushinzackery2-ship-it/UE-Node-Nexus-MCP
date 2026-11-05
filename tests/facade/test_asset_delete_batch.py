"""asset_delete_batch deletes an audited set as one editor transaction."""

import pytest

from ue_node_nexus_mcp.registry.payloads.schema import payload_schema_for
from ue_node_nexus_mcp.registry.specs import get_operation_spec
from ue_node_nexus_mcp.tools import assets as tools_assets

CLOSURE = ["/Game/Retired/BP_Child.BP_Child", "/Game/Retired/BP_Base.BP_Base"]


def test_operation_is_a_high_risk_bridge_write():
    spec = get_operation_spec("asset_delete_batch")
    assert (spec.bridge_operation, spec.kind, spec.risk) == ("asset_delete_batch", "write", "high")


def test_schema_takes_the_whole_set():
    fields = payload_schema_for("asset_delete_batch")["properties"]
    assert fields["asset_paths"]["type"] == "array"
    assert "asset_paths" in payload_schema_for("asset_delete_batch")["required"]
    assert fields["dry_run"]["default"] is True


def test_wrapper_forwards_one_request_for_the_set(monkeypatch):
    calls = []
    monkeypatch.setattr(tools_assets, "_call", lambda operation, payload: calls.append((operation, payload)) or dict(ok=True))
    tools_assets.asset_delete_batch(CLOSURE, dry_run=False)
    assert calls == [("asset_delete_batch", dict(asset_paths=CLOSURE, dry_run=False, allow_referenced=False,
                                                  cleanup_after_delete=True))]


@pytest.mark.parametrize("paths", [[], ["/Game/A.A", ""], ["/Game/A.A", 7], "/Game/A.A"])
def test_wrapper_refuses_a_malformed_set_before_the_editor(monkeypatch, paths):
    monkeypatch.setattr(tools_assets, "_call", lambda *_args: pytest.fail("invalid request reached the editor"))
    with pytest.raises(ValueError, match="asset_paths"):
        tools_assets.asset_delete_batch(paths)
