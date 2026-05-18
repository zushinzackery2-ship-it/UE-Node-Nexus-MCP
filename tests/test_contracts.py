from __future__ import annotations

import pytest

from ue_node_nexus_mcp.contracts import (
    ALL_OPERATIONS,
    READ_OPERATIONS,
    WRITE_OPERATIONS,
    require_list,
    require_mapping,
    require_non_empty_string,
)


def test_operations_are_disjoint() -> None:
    assert READ_OPERATIONS.isdisjoint(WRITE_OPERATIONS)
    assert ALL_OPERATIONS == READ_OPERATIONS | WRITE_OPERATIONS


def test_require_non_empty_string_rejects_blank() -> None:
    with pytest.raises(ValueError):
        require_non_empty_string(" ", "asset_path")


def test_require_mapping_rejects_non_object() -> None:
    with pytest.raises(ValueError):
        require_mapping([], "params")  # type: ignore[arg-type]


def test_require_list_rejects_non_object_items() -> None:
    with pytest.raises(ValueError):
        require_list(["connect"], "operations")  # type: ignore[list-item]
