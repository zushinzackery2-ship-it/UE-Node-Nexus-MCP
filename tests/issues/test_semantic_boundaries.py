"""Typed representation boundaries must preserve author intent and diagnostics."""

from struct import pack

import pytest

from ue_node_nexus_mcp.operation_validation import unknown_field_error
from ue_node_nexus_mcp.transcode.collaboration.semantic.numbers import normalize_number
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync_deps import document_dependencies


@pytest.mark.parametrize("source", ["3.4028234663852886e38", "-3.4028234663852886e38", "1.401298464324817e-45"])
def test_float32_boundaries_use_a_decimal_spelling_with_identical_bits(source):
    result = normalize_number(source, "float")
    assert "e" not in result.lower()
    assert pack("!f", float(result)) == pack("!f", float(source))


@pytest.mark.parametrize("operation,field,replacement", [
    ("graph_snapshot_get", "filters", "node_class_filter (use the filters.node_class value)"),
    ("graph_node_search", "node_class_filter", "filters.node_class"),
    ("graph_node_search", "graph", "graph_name"),
])
def test_graph_filter_errors_explain_the_corresponding_field(operation, field, replacement):
    result = unknown_field_error(operation, dict([(field, "value")]))
    assert result["field_mapping"][field] == replacement


def test_blueprint_contracts_guard_interfaces_and_signature_types():
    document, sink = parse("""nexus: 1
asset: /Game/BP_Main
class: Blueprint

[interfaces]
/Game/BPI_Query.BPI_Query_C

[dispatchers]
Changed(Value: Object(/Game/BP_Data.BP_Data_C))

Collection(Items: Array<Object(/Game/BP_Item.BP_Item_C)>, Values: Map<name,Struct(/Game/S_Value.S_Value)>)

[variables]
Allowed : Set<Class(/Game/BP_Allowed.BP_Allowed_C)>

[function Query() -> (Result: Object(/Game/BP_Result.BP_Result_C))]
""")
    assert not sink.has_errors
    assert document_dependencies(document, "blueprint") == set((
        "/Game/BPI_Query.BPI_Query", "/Game/BP_Data.BP_Data", "/Game/BP_Result.BP_Result",
        "/Game/BP_Item.BP_Item", "/Game/S_Value.S_Value", "/Game/BP_Allowed.BP_Allowed",
    ))
