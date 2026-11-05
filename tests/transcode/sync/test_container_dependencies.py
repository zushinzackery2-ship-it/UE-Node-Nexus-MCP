"""Dependency collection follows container/member types instead of flattening them."""

import pytest

from ue_node_nexus_mcp.transcode.sync.dependencies import document_dependencies
from ue_node_nexus_mcp.transcode.sync.dependencies.values import schema_prop_hint, value_dependencies
from ue_node_nexus_mcp.transcode.text.parser import parse


@pytest.mark.parametrize("type_text,text,expected", [
    ("Map<string,Object(/Script/Engine.Texture2D)>", '(("/Game/Key.Key",Texture2D\'/Game/T.T\'))', {"/Game/T.T"}),
    ("Map<Object(/Script/Engine.Texture2D),string>", '((Texture2D\'/Game/T.T\',"/Game/Value.Value"))', {"/Game/T.T"}),
    ("Struct(/Script/SlateCore.SlateBrush)", "(ResourceObject=Texture2D'/Game/T.T')", {"/Game/T.T"}),
    ("Array<Struct(/Script/SlateCore.SlateBrush)>", "((ResourceObject=Texture2D'/Game/T.T'))", {"/Game/T.T"}),
    ("Array<string>", '("/Game/Value.Value")', set()),
    ("Map<string,name>", '(("/Game/Key.Key","/Game/Value.Value"))', set()),
])
def test_variable_container_dependencies_preserve_string_boundaries(type_text, text, expected):
    document, errors = parse("nexus: 1\nasset: /Game/BP\nclass: Blueprint\n\n"
                             f"[variables]\nValue : {type_text} = {text}\n")
    assert not errors.has_errors
    assert document_dependencies(document, "blueprint") == expected


def test_reflected_nested_members_keep_their_own_contract():
    contract = dict(kind="array", element=dict(kind="struct", fields=dict(
        Label=dict(kind="string", type="FString"),
        Assets=dict(kind="map", key=dict(kind="name", type="FName"),
                    value=dict(kind="object", type="UTexture*")))))
    hint = schema_prop_hint(dict(value_schema=contract))
    text = '((Label="/Game/Label.Label",Assets=(("/Game/Key.Key",Texture2D\'/Game/T.T\'))))'
    assert value_dependencies(text, "/Game/BP.BP", hint) == {"/Game/T.T"}
