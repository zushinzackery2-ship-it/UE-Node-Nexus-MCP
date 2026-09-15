"""Class-name aliases: how a class record answers to path, real name and stripped form."""

from __future__ import annotations

import json

from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock


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
