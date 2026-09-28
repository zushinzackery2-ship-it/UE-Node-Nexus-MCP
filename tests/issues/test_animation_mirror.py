"""Issues3 #3: animation Blueprint headers retain their real asset class."""

import pytest

from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse


@pytest.mark.parametrize("name", ("Blueprint", "AnimBlueprint"))
def test_blueprint_header_accepts_supported_asset_class(name):
    document, parsed = parse(
        f"nexus: 1\nasset: /Game/Test/Animation\nclass: {name}\nschema: test\n"
        "\n[asset]\nParentClass = /Script/Engine.AnimInstance\n"
        "\n[variables]\nSpeed : float = 0 {Transient}\n"
        "\n[graph EventGraph]\n")
    assert not parsed.errors()
    assert not lint_document(document, "blueprint", None).errors()


def test_blueprint_header_rejects_unrelated_asset_class():
    document, _ = parse("nexus: 1\nasset: /Game/Test/Invalid\nclass: Material\nschema: test\n")
    assert [item.code for item in lint_document(document, "blueprint", None).errors()] == ["class_kind_mismatch"]
