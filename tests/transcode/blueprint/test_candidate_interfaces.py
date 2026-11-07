"""A candidate signature supersedes previously allocated caller pins."""

import pytest

from ue_node_nexus_mcp.transcode.blueprint.pins import node_pins, pin
from ue_node_nexus_mcp.transcode.collaboration.semantic.interfaces import documents
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.text.parser import parse


def document(asset, body):
    result, sink = parse(f"nexus: 1\nasset: {asset}\nclass: Blueprint\nschema: key\n{body}")
    assert not sink.has_errors
    return result


@pytest.mark.parametrize("graph", ["graph EventGraph", "function Caller()"])
@pytest.mark.parametrize("old,new", [(None, "Valid"), ("Old", "Valid"), ("Valid", None)])
def test_local_signature_replaces_old_pins_without_changing_node_identity(graph, old, new):
    result = f" -> ({new}: bool)" if new else ""
    doc = document("/Game/BP_A", f"[function Apply(){result}]\n[{graph}]\ncall : CallFunction(self.Apply)\n")
    section = doc.find_sections(graph.split()[0])[-1]
    decl = section.decls()[0]
    decl.meta["pins"] = [pin("execute", "in"), pin("then", "out"), pin("self", "in", "object")]
    if old:
        decl.meta["pins"].append(pin(old, "out", "bool", guid="old-pin"))
    actual = node_pins(decl, doc, section)
    assert [p["name"] for p in actual if p["dir"] == "out" and p["type"]["category"] != "exec"] == ([new] if new else [])
    assert any(p["name"] == "self" for p in actual)
    assert decl.id == "call"


@pytest.mark.parametrize("owner", ["/Game/BP_A.BP_A_C", "/Game/BP_A", "BP_A_C"])
def test_cross_asset_candidate_replaces_catalog_and_physical_pins(owner):
    provider = document("/Game/BP_A", "[function Apply() -> (Valid: bool)]\n")
    consumer = document("/Game/BP_B", f"[graph EventGraph]\ncall : CallFunction({owner}.Apply)\n"
                        "branch : Branch\ncall.Valid -> branch.Condition\n")
    section = consumer.sections[0]
    call = section.decls()[0]
    call.meta["pins"] = [pin("execute", "in"), pin("then", "out")]
    assert lint_document(consumer, "blueprint", None).has_errors
    with documents([("blueprint", provider), ("blueprint", consumer)]):
        assert not lint_document(consumer, "blueprint", None).has_errors
        assert "Valid" in [p["name"] for p in node_pins(call, consumer, section)]
        removed = document("/Game/BP_A", "[function Apply()]\n")
        with documents([("blueprint", removed)]):
            errors = lint_document(consumer, "blueprint", None).errors()
            assert any(e.code == "unknown_pin" and "Valid" in e.message for e in errors)
        assert not lint_document(consumer, "blueprint", None).has_errors
    assert lint_document(consumer, "blueprint", None).has_errors


def test_matching_physical_pin_binding_is_retained():
    doc = document("/Game/BP_A", "[function Apply() -> (Valid: bool)]\n[graph EventGraph]\ncall : CallFunction(self.Apply)\n")
    section = doc.sections[-1]
    call = section.decls()[0]
    call.meta["pins"] = [pin("Valid", "out", "bool", guid="stable")]
    actual = node_pins(call, doc, section)
    assert next(p for p in actual if p["name"] == "Valid")["guid"] == "stable"


def test_plan_refreshes_changed_call_before_wiring_without_recreating_it():
    from ue_node_nexus_mcp.transcode.diff.service import build_plan

    provider = document("/Game/BP_A", "[function Apply() -> (Valid: bool)]\n")
    body = "[graph EventGraph]\ncall : CallFunction(/Game/BP_A.BP_A_C.Apply)\nbranch : Branch\n"
    before = document("/Game/BP_B", body)
    before.sections[0].decls()[0].meta["pins"] = [pin("execute", "in"), pin("then", "out")]
    after = document("/Game/BP_B", body + "call.Valid -> branch.Condition\n")
    with documents([("blueprint", provider)]):
        plan = build_plan(after, before, "blueprint")
    operations = [verb.op for verb in plan.verbs]
    assert operations == ["refresh_function_call", "connect_pins"]
    assert plan.verbs[0].args["id"] == "call"


def test_changed_pin_type_does_not_reuse_physical_binding():
    doc = document("/Game/BP_A", "[function Apply() -> (Valid: float)]\n[graph EventGraph]\ncall : CallFunction(self.Apply)\n")
    section = doc.sections[-1]
    call = section.decls()[0]
    call.meta["pins"] = [pin("Valid", "out", "bool", guid="old-bool")]
    actual = next(p for p in node_pins(call, doc, section) if p["name"] == "Valid")
    assert "guid" not in actual


def test_deleted_candidate_function_cannot_be_borrowed_from_old_schema(tmp_path):
    from ue_node_nexus_mcp.transcode.schema.catalog import publish
    from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

    directory = tmp_path / "schema"
    publish(directory, "key", dict(functions={"/Game/BP_A.BP_A_C.Removed": dict(params=[]),
                                              "/Script/Engine.Actor.Inherited": dict(params=[])}))
    schema = SchemaLock(directory, "key")
    provider = document("/Game/BP_A", "[asset]\nParentClass = /Script/Engine.Actor\n")
    consumer = document("/Game/BP_B", "[graph EventGraph]\ncall : CallFunction(/Game/BP_A.BP_A_C.Removed)\n")
    with documents([("blueprint", provider)]):
        assert schema.function("/Game/BP_A.BP_A_C", "Removed") is None
        assert schema.function("/Game/BP_A.BP_A_C", "Inherited") is not None
        assert any(e.code == "unknown_function" for e in lint_document(consumer, "blueprint", schema).errors())
    parent = document("/Game/BP_Parent", "[function New() -> (Accepted: bool)]\n")
    child = document("/Game/BP_Child", "[asset]\nParentClass = /Game/BP_Parent.BP_Parent_C\n")
    with documents([("blueprint", parent), ("blueprint", child)]):
        assert schema.function("/Game/BP_Child.BP_Child_C", "New")["params"][0]["name"] == "Accepted"
