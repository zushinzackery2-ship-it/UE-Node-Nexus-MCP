from __future__ import annotations

from ue_node_nexus_mcp.contracts import ALL_OPERATIONS
from ue_node_nexus_mcp.payload_schema import (
    derive_schema,
    example_payload_for,
    payload_schema_for,
)


def sample_wrapper(asset_path: str, dry_run: bool = True) -> None:
    del asset_path, dry_run


def test_derive_schema_preserves_required_fields_and_defaults() -> None:
    schema = derive_schema(sample_wrapper)

    assert schema == {
        "type": "object",
        "properties": {
            "asset_path": {"type": "string"},
            "dry_run": {"type": "boolean", "default": True},
        },
        "required": ["asset_path"],
    }


def test_every_operation_exposes_an_object_schema_and_example() -> None:
    for operation in ALL_OPERATIONS:
        schema = payload_schema_for(operation)
        example = example_payload_for(operation)

        assert schema["type"] == "object", operation
        assert isinstance(example, dict), operation
        assert set(schema.get("required", ())).issubset(example), operation


def test_patch_operations_keep_item_level_schema() -> None:
    schema = payload_schema_for("graph_patch_apply")
    operation_items = schema["properties"]["operations"]["items"]

    assert operation_items["required"] == ["op"]
    assert "connect_pins" in operation_items["properties"]["op"]["enum"]
