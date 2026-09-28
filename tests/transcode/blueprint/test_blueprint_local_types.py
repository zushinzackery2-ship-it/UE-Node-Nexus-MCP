"""Function-local types must survive the same semantic round trip used by lint."""

import pytest

from ue_node_nexus_mcp.transcode.blueprint.types import join_type_text, parse_type_text
from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, text_of
from ue_node_nexus_mcp.transcode.lint.service import lint_document


@pytest.mark.parametrize("type_text", [
    "Array<Object(/Script/Engine.PrimitiveComponent)>",
    "Array<Object(/Script/Engine.MaterialInstanceDynamic)>",
    "Set<SoftObject(/Script/Engine.Texture2D)>",
    "Map<name,Object(/Script/Engine.Actor)>",
    "Struct(/Script/CoreUObject.Vector)",
])
def test_function_local_container_type_survives_workspace_capture(type_text):
    source = ("nexus: 1\nasset: /Game/BFL_LocalTypes\nclass: Blueprint\n"
              "[function Gather()]\nlocal Items : " + type_text + "\n"
              "read_items : VariableGet(Items)\n")
    snapshot = capture(source, None, "local-types", "blueprint")
    document = to_document(snapshot)
    local = next(decl for section in document.sections for decl in section.decls()
                 if decl.modifier == "local")
    actual = join_type_text(local.type_name, local.args)
    assert parse_type_text(actual) == parse_type_text(type_text)
    assert not lint_document(document, "blueprint", None).has_errors
    repeated = capture(text_of(snapshot), snapshot, "local-types", "blueprint")
    assert repeated["semantic"] == snapshot["semantic"]
