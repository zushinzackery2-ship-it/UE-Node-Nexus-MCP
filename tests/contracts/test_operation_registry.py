from __future__ import annotations

from ue_node_nexus_mcp.operation_registry import get_operation_spec, operation_specs


def test_registry_distinguishes_local_handlers_from_bridge_operations() -> None:
    assert get_operation_spec("material_lint").local_mcp is True
    assert get_operation_spec("material_lint").bridge_operation is None
    assert get_operation_spec("asset_get").bridge_operation == "asset_get"


def test_every_operation_has_a_meaningful_summary() -> None:
    for name, spec in operation_specs().items():
        assert spec.summary.strip(), name
