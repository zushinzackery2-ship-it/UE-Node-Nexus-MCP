"""A valid preflight must agree with the executor's field contract."""

from ue_node_nexus_mcp.facade_plan import ue_plan_validate
from ue_node_nexus_mcp.tools_facade import ue_execute


def test_plan_rejects_the_same_unknown_field_as_execute(all_features):
    payload = dict(asset_path="/Game/Test/M", graph="EventGraph")
    planned = ue_plan_validate([dict(operation="graph_snapshot_get", payload=payload)])
    executed = ue_execute("graph_snapshot_get", payload)
    assert planned["ok"] is False
    assert planned["data"]["valid"] is False
    error = planned["data"]["errors"][0]
    assert error["code"] == executed["error"]["code"] == "unknown_field"
    assert error["fields"] == ["graph"]
    assert error["field_mapping"] == dict(graph="graph_name")
    assert planned["data"]["estimated_changes"] == []


def test_plan_keeps_valid_items_and_all_bad_item_positions(all_features):
    operations = [dict(operation="graph_snapshot_get", payload=dict(asset_path="/Game/Test/M")),
                  dict(operation="graph_snapshot_get", payload=dict()),
                  dict(operation="graph_snapshot_get", payload=dict(asset_path="/Game/Test/M", typo=True))]
    data = ue_plan_validate(operations)["data"]
    assert data["valid"] is False
    assert [item["index"] for item in data["errors"]] == [1, 2]
    assert [item["code"] for item in data["errors"]] == ["missing_required_field", "unknown_field"]
    assert data["operation_count"] == 3
    assert data["estimated_changes"] == [["read", "graph_snapshot_get", "/Game/Test/M"]]


def test_plan_accepts_declared_fields_without_bridge_contact(all_features):
    result = ue_plan_validate([dict(operation="graph_snapshot_get", payload=dict(
        asset_path="/Game/Test/M", graph_name="EventGraph"))])
    assert result["ok"] is True
    assert result["data"]["errors"] == []
