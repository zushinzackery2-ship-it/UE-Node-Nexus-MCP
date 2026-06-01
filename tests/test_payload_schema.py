from __future__ import annotations

from ue_node_nexus_mcp import server
from ue_node_nexus_mcp.contracts import BRIDGE_OPERATIONS
from ue_node_nexus_mcp.operation_registry import get_operation_spec, operation_schema
from ue_node_nexus_mcp.payload_schema import example_payload_for, payload_schema_for


def test_every_bridge_operation_has_a_real_payload_schema() -> None:
    for operation in BRIDGE_OPERATIONS:
        schema = payload_schema_for(operation)
        assert schema["type"] == "object"
        # A real derived schema always exposes a properties map; the generic
        # placeholder (used only when a wrapper is missing) does not.
        assert "properties" in schema, f"{operation} fell back to the placeholder schema"


def test_asset_create_schema_captures_enum_required_and_defaults() -> None:
    schema = payload_schema_for("asset_create")
    properties = schema["properties"]

    assert set(schema["required"]) == {"asset_path", "asset_kind"}
    assert properties["asset_path"] == {"type": "string"}
    assert properties["asset_kind"]["type"] == "string"
    assert properties["asset_kind"]["enum"] == [
        "material",
        "material_instance",
        "blueprint",
        "material_function",
        "data_asset",
        "texture_render_target_2d",
    ]
    assert properties["dry_run"] == {"type": "boolean", "default": True}
    assert properties["save"] == {"type": "boolean", "default": False}
    assert properties["parent_asset_path"] == {"type": "string", "nullable": True, "default": None}


def test_list_and_object_params_map_to_array_and_object() -> None:
    move_batch = payload_schema_for("asset_move_batch")
    assert move_batch["properties"]["items"] == {"type": "array"}
    assert move_batch["required"] == ["items"]

    params_set = payload_schema_for("node_params_set")
    assert params_set["properties"]["params"] == {"type": "object"}
    assert "asset_path" in params_set["required"]
    assert "node_id" in params_set["required"]


def test_no_arg_read_operations_have_empty_properties() -> None:
    schema = payload_schema_for("level_current_get")
    assert schema["properties"] == {}
    assert "required" not in schema


def test_operation_risk_reflects_destructiveness() -> None:
    assert get_operation_spec("asset_delete").risk == "high"
    assert get_operation_spec("folder_delete").risk == "high"
    assert get_operation_spec("auto_index_clear").risk == "high"
    assert get_operation_spec("editor_save_all").risk == "high"
    assert get_operation_spec("editor_request_exit").risk == "high"
    assert get_operation_spec("asset_get").risk == "low"
    assert get_operation_spec("graph_snapshot_get").risk == "low"
    assert get_operation_spec("node_params_set").risk == "medium"
    assert get_operation_spec("asset_create").risk == "medium"


def test_operation_schema_embeds_payload_schema() -> None:
    schema = operation_schema(get_operation_spec("asset_create"))
    assert schema["operation"] == "asset_create"
    assert schema["payload_schema"]["properties"]["asset_path"] == {"type": "string"}


def test_facade_capability_schema_returns_real_fields() -> None:
    response = server.ue_capability_get(operation="asset_create", detail="schema")
    assert response["ok"] is True
    payload_schema = response["data"]["payload_schema"]
    assert "asset_path" in payload_schema["properties"]
    assert payload_schema["properties"]["asset_kind"]["enum"][0] == "material"


def test_example_payload_uses_required_fields_and_safe_dry_run_default() -> None:
    example = example_payload_for("asset_create")

    assert "asset_path" in example
    assert example["asset_kind"] == "material"
    assert example["dry_run"] is True
    assert "save" not in example
