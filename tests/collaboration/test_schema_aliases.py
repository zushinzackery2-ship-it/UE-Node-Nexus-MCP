"""Class-name aliases: how a class record answers to path, real name and stripped form."""

from __future__ import annotations

import json

from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.diff_common import class_path
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

CALL_FUNCTION_CATALOG = (
    "/Script/BlueprintGraph.K2Node_CallFunction",
    "/Script/AnimGraph.AnimGraphNode_CallFunction",
)
CALL_FUNCTION_TEXT = (
    "nexus: 1\nasset: /Game/Blueprints/BP_Call\nclass: Blueprint\nschema: key\n\n"
    "[graph EventGraph]\ncall : CallFunction(KismetSystemLibrary.PrintString) @ 0,0\n"
)


def _call_function_lock(tmp_path):
    """Catalog laid out the way the plugin writes it: record keys are full UE paths."""
    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", dict(k2node={path: dict(path=path, props=dict()) for path in CALL_FUNCTION_CATALOG}))
    return SchemaLock(directory, "key")


def test_class_aliases_answer_to_path_real_name_and_stripped_form(tmp_path):
    """Class tables are keyed by full UE path, so the class name itself must be an alias.

    ``short_class_name`` strips engine-family prefixes for the node ids the mirror
    writes, which leaves classes whose real name starts with one of those tokens
    (NiagaraComponent, MaterialExpressionConstant) without their own name.
    """
    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", {
        "component": {
            "/Script/Niagara.NiagaraComponent": dict(path="/Script/Niagara.NiagaraComponent", props=dict()),
            "/Script/Engine.StaticMeshComponent": dict(path="/Script/Engine.StaticMeshComponent", props=dict()),
        },
        "asset": {"/Script/Niagara.NiagaraActor": dict(path="/Script/Niagara.NiagaraActor", props=dict())},
        "material_expression": {"/Script/Engine.MaterialExpressionConstant": dict(path="/Script/Engine.MaterialExpressionConstant", props=dict())},
        "k2node": {"/Script/BlueprintGraph.K2Node_IfThenElse": dict(path="/Script/BlueprintGraph.K2Node_IfThenElse", props=dict())},
    })
    lock = SchemaLock(directory, "key")

    for spelling in ("NiagaraComponent", "/Script/Niagara.NiagaraComponent", "Component"):
        info = lock.resolve_class("component", spelling)
        assert info is not None, spelling
        assert info.path == "/Script/Niagara.NiagaraComponent", spelling
    assert lock.resolve_class("component", "StaticMeshComponent").path == "/Script/Engine.StaticMeshComponent"
    assert lock.resolve_class("asset", "NiagaraActor").path == "/Script/Niagara.NiagaraActor"
    assert lock.resolve_class("material_expression", "Constant").path == "/Script/Engine.MaterialExpressionConstant"
    assert lock.resolve_class("material_expression", "MaterialExpressionConstant").path == "/Script/Engine.MaterialExpressionConstant"
    assert lock.resolve_class("k2node", "IfThenElse").path == "/Script/BlueprintGraph.K2Node_IfThenElse"


def test_catalogs_published_before_the_alias_fix_resolve_through_the_entry_path(tmp_path):
    """An already-published catalog carries no real class name; the entry path supplies it."""
    directory = tmp_path / ".nexus" / "schema" / "key"
    path = "/Script/Niagara.NiagaraComponent"
    publish(directory, "key", dict(component={path: dict(path=path, props=dict())}))
    manifest = json.loads((directory / "key.json").read_text(encoding="utf-8"))
    manifest["tables"]["component"][path]["aliases"] = [path, "Component"]
    (directory / "key.json").write_text(json.dumps(manifest), encoding="utf-8")

    lock = SchemaLock(directory, "key")
    assert "NiagaraComponent" not in manifest["tables"]["component"][path]["aliases"]
    assert lock.resolve_class("component", "NiagaraComponent").path == path


def test_call_function_short_name_is_pinned_to_the_blueprint_node(tmp_path):
    """``CallFunction`` names both records once the engine prefixes are stripped.

    The plain spelling means the Blueprint node, while the AnimGraph node keeps its
    own spelling, so neither lookup may come back as ambiguous.
    """
    lock = _call_function_lock(tmp_path)

    assert lock.resolve_class("k2node", "CallFunction").path == "/Script/BlueprintGraph.K2Node_CallFunction"
    assert lock.resolve_class("k2node", "K2Node_CallFunction").path == "/Script/BlueprintGraph.K2Node_CallFunction"
    assert lock.resolve_class("k2node", "AnimGraphNode_CallFunction").path == "/Script/AnimGraph.AnimGraphNode_CallFunction"
    assert class_path(lock, "k2node", "CallFunction") == "/Script/BlueprintGraph.K2Node_CallFunction"


def test_a_call_function_document_lints_and_plans_against_an_animgraph_schema(tmp_path):
    """Lint resolves node classes without a tolerant path, so an unresolved spelling fails a push."""
    lock = _call_function_lock(tmp_path)
    document, sink = parse(CALL_FUNCTION_TEXT)
    assert sink.items == []

    assert lint_document(document, "blueprint", lock, "BP_Call.bp.nexus").items == []
    plan = build_plan(document, document, "blueprint", dict(), lock)
    assert plan.verbs == [] and not plan.has_errors
