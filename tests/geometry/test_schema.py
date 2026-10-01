"""Meaningful malformed recipe and discovery coverage."""

import copy

import jsonschema
import pytest

from ue_node_nexus_mcp.contracts import BRIDGE_OPERATIONS
from ue_node_nexus_mcp.payload_schema import example_payload_for, payload_schema_for


def test_geometry_discovered_and_example_valid():
    for operation in ("mesh_geometry_get", "mesh_geometry_build"):
        assert operation in BRIDGE_OPERATIONS
        jsonschema.validate(example_payload_for(operation), payload_schema_for(operation))


@pytest.mark.parametrize("bad", [
    dict(op="lattice", dimensions=[1, 3, 2], offsets=[]),
    dict(op="lattice", dimensions=[3, 3, 2], offsets=[dict(index=[-1, 0, 0], delta=[0, 0, 1])]),
    dict(op="noise", scale=0),
    dict(op="noise", direction="world"),
    dict(op="remesh", target_edge_length=0),
    dict(op="remesh", target_edge_length=5, iterations=100),
    dict(op="unknown"),
])
def test_invalid_operations_rejected(bad):
    payload = copy.deepcopy(example_payload_for("mesh_geometry_build"))
    payload["recipe"]["ops"] = [bad]
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(payload, payload_schema_for("mesh_geometry_build"))


def test_sources_mutually_exclusive():
    payload = copy.deepcopy(example_payload_for("mesh_geometry_build"))
    payload["recipe"].update(source_asset="/Game/Base", source_revision="revision")
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(payload, payload_schema_for("mesh_geometry_build"))
