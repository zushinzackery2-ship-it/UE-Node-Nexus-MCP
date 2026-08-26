from __future__ import annotations

import pytest

from ue_node_nexus_mcp.contracts import ALL_OPERATIONS
from ue_node_nexus_mcp.operation_registry import (
    capability_index,
    get_operation_spec,
    operation_specs,
)


def test_registry_reads_manifest_metadata() -> None:
    assert get_operation_spec("asset_delete").risk == "high"
    assert get_operation_spec("asset_get").risk == "low"
    assert get_operation_spec("asset_move").risk == "medium"
    assert get_operation_spec("graph_node_info_get").default_response == "full"
    assert get_operation_spec("asset_create").default_response == "delta"
    assert get_operation_spec("editor_save_all").hidden is True
    assert get_operation_spec("material_lint").local_mcp is True
    assert get_operation_spec("material_lint").bridge_operation is None
    assert get_operation_spec("asset_get").bridge_operation == "asset_get"


def test_every_operation_has_a_meaningful_summary() -> None:
    for name, spec in operation_specs().items():
        assert spec.summary.strip(), name
        assert spec.summary != name.replace("_", " ").capitalize(), name


def test_unknown_operation_raises_value_error() -> None:
    with pytest.raises(ValueError):
        get_operation_spec("no_such_operation")


def test_capability_index_hidden_filter() -> None:
    groups = {spec.group for spec in operation_specs().values()}
    visible = {row[0] for row in capability_index(groups)}
    everything = {row[0] for row in capability_index(groups, include_hidden=True)}

    assert "editor_request_exit" not in visible
    assert "editor_request_exit" in everything
    assert everything == ALL_OPERATIONS
