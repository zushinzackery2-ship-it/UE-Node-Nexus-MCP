"""Author pin aliases canonicalize to the engine names on both comparison sides."""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.fixture
def schema(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node={
        "/Script/BlueprintGraph.K2Node_GetArrayItem": dict(
            path="/Script/BlueprintGraph.K2Node_GetArrayItem", dynamic_pins=True,
            pin_aliases={"TargetArray": "Array", "Index": "Dimension 1", "Item": "Output"}),
    }))
    return SchemaLock(directory, "key")


HEADER = "nexus: 1\nasset: /Game/BP_Alias\nclass: Blueprint\nschema: key\n\n"


def _document(body):
    document, sink = parse(HEADER + body)
    assert not sink.has_errors, sink.items
    return document


def test_aliases_canonicalize_links_to_engine_names(schema):
    document = _document("[graph EventGraph]\n"
                         "values : VariableGet(Values)\n"
                         "item : GetArrayItem\n"
                         "values -> item.TargetArray\n")
    semantic, _, _ = encode(document, "blueprint", None, "author", schema)
    links = semantic["sections"]["graph:EventGraph"]["links"]
    assert [link["dst_pin"] for link in links.values()] == ["Array"]


def test_alias_and_engine_name_compare_equal(schema):
    aliased = _document("[graph EventGraph]\n"
                        "values : VariableGet(Values)\n"
                        "item : GetArrayItem\n"
                        "values -> item.TargetArray\n")
    native = _document("[graph EventGraph]\n"
                       "values : VariableGet(Values)\n"
                       "item : GetArrayItem\n"
                       "values -> item.Array\n")
    plan = build_plan(native, aliased, "blueprint", None, schema)
    assert plan.verbs == [] and not plan.has_errors
