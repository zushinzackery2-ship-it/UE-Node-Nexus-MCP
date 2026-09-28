"""Candidate interfaces replace stale catalogs only inside that candidate."""

from types import SimpleNamespace

from ue_node_nexus_mcp.transcode.collaboration.workspace.lint import lint_root
from ue_node_nexus_mcp.transcode.material.interfaces import documents
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock


PROVIDER = "nexus: 1\nasset: /Game/MF_A\nclass: MaterialFunction\nschema: key\n[graph]\nresult : FunctionOutput(OutputName=New)\n"
CONSUMER = "nexus: 1\nasset: /Game/M_A\nclass: Material\nschema: key\n[graph]\ncall : MaterialFunctionCall(MaterialFunction=/Game/MF_A)\ncall.New -> out.EmissiveColor\n"


def schema_at(tmp_path):
    directory = tmp_path / "schema"
    publish(directory, "key", dict(material_expression=dict(
        MaterialExpressionFunctionOutput=dict(path="/Script/Engine.MaterialExpressionFunctionOutput",
            props=dict(OutputName=dict(type="FString"))),
        MaterialExpressionMaterialFunctionCall=dict(path="/Script/Engine.MaterialExpressionMaterialFunctionCall",
            props=dict(MaterialFunction=dict(type="object")))),
        material_functions=dict([("/Game/MF_A.MF_A", dict(outputs=[dict(name="Old")], inputs=[]))])))
    return SchemaLock(directory, "key")


def test_candidate_signature_isolated_from_catalog_and_other_candidates(tmp_path):
    schema = schema_at(tmp_path)
    provider, _ = parse(PROVIDER)
    consumer, _ = parse(CONSUMER)
    assert lint_document(consumer, "material", schema).has_errors
    with documents([("material_function", provider)]):
        assert not lint_document(consumer, "material", schema).has_errors
        removed, _ = parse(PROVIDER.replace("New", "Removed"))
        with documents([("material_function", removed)]):
            assert lint_document(consumer, "material", schema).has_errors
        assert not lint_document(consumer, "material", schema).has_errors
    assert lint_document(consumer, "material", schema).has_errors
    assert schema.material_function("/Game/MF_A")["outputs"][0]["name"] == "Old"


def test_offline_root_lint_uses_complete_candidate(tmp_path):
    schema = schema_at(tmp_path)
    root = tmp_path / "files"
    root.mkdir()
    (root / "MF_A.mf.nexus").write_text(PROVIDER, encoding="utf-8")
    (root / "M_A.mat.nexus").write_text(CONSUMER, encoding="utf-8")
    result = lint_root(SimpleNamespace(schema=schema, schema_key="key"), str(root))
    assert result["error_count"] == 0, result
    assert len(result["rows"]) == 2


def test_partial_lint_does_not_use_unselected_worktree_provider(tmp_path):
    schema = schema_at(tmp_path)
    root = tmp_path / "files"
    root.mkdir()
    (root / "MF_A.mf.nexus").write_text(PROVIDER, encoding="utf-8")
    (root / "M_A.mat.nexus").write_text(CONSUMER, encoding="utf-8")
    result = lint_root(SimpleNamespace(schema=schema, schema_key="key"), str(root), ["M_A.mat.nexus"])
    assert result["error_count"] == 1
    assert "unknown_pin" in result["diagnostics"][0]


def test_schema_free_candidate_numeric_output_keeps_declared_name():
    from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, text_of

    provider, _ = parse(PROVIDER)
    with documents([("material_function", provider)]):
        snapshot = capture(CONSUMER.replace("call.New", "call.0"), None, "test", "material")
        assert "call.New ->" in text_of(snapshot)


def test_immutable_candidate_trees_do_not_inherit_active_workspace(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
    from ue_node_nexus_mcp.transcode.collaboration.store import Store
    from ue_node_nexus_mcp.transcode.material.interfaces import tree

    store = Store(tmp_path / "repository")
    old = capture(PROVIDER.replace("New", "Old"), None, "old", "material_function")
    new = capture(PROVIDER, old, "new", "material_function")
    entries = [dict(provider=store.objects.put("snapshot", state)) for state in (old, new)]
    consumer, _ = parse(CONSUMER)
    with tree(store, entries[1]):
        assert not lint_document(consumer, "material", None).has_errors
        with tree(store, entries[0]):
            assert lint_document(consumer, "material", None).has_errors
        assert not lint_document(consumer, "material", None).has_errors
