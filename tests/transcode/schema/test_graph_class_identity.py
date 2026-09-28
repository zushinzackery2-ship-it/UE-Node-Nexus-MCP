"""Shared reflected class names must survive export, semantic storage and diff."""

import pytest

from ue_node_nexus_mcp.transcode.raw.codec import document_from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.diff.common import same_class
from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from tests.transcode.fixtures import blueprint_raw, material_raw, niagara_raw


@pytest.mark.parametrize("family,factory,section", [
    ("material_expression", material_raw, "graph"),
    ("k2node", blueprint_raw, "graph"),
    ("niagara_renderer", niagara_raw, "renderers"),
])
def test_shared_graph_classes_survive_document_round_trip(tmp_path, family, factory, section):
    raw = factory()
    if family == "k2node":
        nodes = raw["blueprint"]["graphs"][0]["nodes"]
    elif family == "niagara_renderer":
        nodes = raw["niagara"]["emitters"][0]["renderers"]
    else:
        nodes = raw["graph"]["nodes"]
    node = next(item for item in nodes if item.get("enabled", True))
    path = node["class"]
    other = "/Script/OtherPlugin." + path.rsplit(".", 1)[-1]
    tables = dict()
    tables[family] = dict((name, dict(path=name, props=dict())) for name in (path, other))
    publish(tmp_path, "key", tables)
    schema = SchemaLock(tmp_path, "key")

    document, _, _ = document_from_raw(raw, schema=schema)
    declaration = next(decl for group in document.find_sections(section) for decl in group.decls()
                       if decl.meta.get("guid") == node["guid"])
    assert declaration.type_name == path
    parsed, sink = parse(emit(document))
    assert not sink.has_errors
    exported = next(decl for group in parsed.find_sections(section) for decl in group.decls()
                    if decl.id == declaration.id)
    assert exported.type_name == path


def test_blueprint_semantics_preserve_unambiguous_full_class_identity():
    path = "/Script/OtherPlugin.K2Node_CustomAction"
    document, sink = parse(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
        "\n[graph EventGraph]\nnode : " + path + " @ 0,0\n")
    assert not sink.has_errors
    semantic, _, _ = encode(document, "blueprint", None, "test")
    entity = next(iter(semantic["sections"]["graph:EventGraph"]["entities"].values()))
    assert entity["type"] == path


def test_blueprint_full_paths_do_not_compare_equal_by_short_name():
    assert not same_class(None, "k2node", "/Script/PluginA.K2Node_CustomAction",
                          "/Script/PluginB.K2Node_CustomAction")


def test_class_comparison_does_not_swallow_catalog_corruption():
    from ue_node_nexus_mcp.transcode.errors import SyncError

    class CorruptSchema:
        def resolve_class(self, family, name):
            raise SyncError("schema_corrupt", "record checksum mismatch")

    with pytest.raises(SyncError, match="checksum"):
        same_class(CorruptSchema(), "component", "Mesh", "/Script/Engine.Mesh")


def test_function_owner_collision_survives_export_and_semantic_encoding(tmp_path):
    from ue_node_nexus_mcp.transcode.raw.nodes import node_decl

    owner = "/Script/PluginA.SharedLibrary"
    full = owner + ".Run"
    other = "/Script/PluginB.SharedLibrary.Run"
    publish(tmp_path, "key", dict(callable_functions=dict(
        (path, dict(path=path, params=[])) for path in (full, other))))
    schema = SchemaLock(tmp_path, "key")
    node = dict(guid="call", class_short="CallFunction", config=dict(function_owner=owner, function_name="Run"))
    node["class"] = "/Script/BlueprintGraph.K2Node_CallFunction"
    declaration = node_decl("call", node, schema)
    assert declaration.positional() == [full]
    document, _ = parse("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
                        "\n[graph EventGraph]\ncall : CallFunction(" + full + ") @ 0,0\n")
    semantic, _, _ = encode(document, "blueprint", None, "test", schema)
    entity = next(iter(semantic["sections"]["graph:EventGraph"]["entities"].values()))
    assert entity["positional"][0]["value"] == full
