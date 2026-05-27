from __future__ import annotations

from ue_node_nexus_mcp.contracts import ALL_OPERATIONS, DEFAULT_HIDDEN_OPERATIONS, LOCAL_MCP_OPERATIONS
from ue_node_nexus_mcp.operation_registry import get_operation_spec, operation_specs


def test_operation_registry_covers_all_legacy_operations() -> None:
    specs = operation_specs()

    assert set(specs) == ALL_OPERATIONS
    assert specs["asset_list"].kind == "read"
    assert specs["node_params_set"].kind == "write"


def test_operation_registry_tracks_hidden_and_local_operations() -> None:
    specs = operation_specs()

    for operation in DEFAULT_HIDDEN_OPERATIONS:
        assert specs[operation].hidden_from_legacy is True
    for operation in LOCAL_MCP_OPERATIONS:
        assert specs[operation].local_mcp is True
        assert specs[operation].bridge_operation is None


def test_get_operation_spec_rejects_unknown_operation() -> None:
    try:
        get_operation_spec("python_exec")
    except ValueError as exc:
        assert "unknown operation" in str(exc)
    else:
        raise AssertionError("unknown operation should fail")
