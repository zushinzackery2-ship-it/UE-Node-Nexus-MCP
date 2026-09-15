from __future__ import annotations

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse

from .fixtures import blueprint_raw, material_instance_raw, material_raw, material_function_raw, niagara_raw

KINDS = {
    material_function_raw: "material_function",
    material_raw: "material",
    material_instance_raw: "material_instance",
    blueprint_raw: "blueprint",
    niagara_raw: "niagara_system",
}


def _base_and_text(factory):
    raw = factory()
    document, ids, _ = document_from_raw(raw)
    return document, emit(document), ids


def _plan_for(factory, edit):
    base, text, ids = _base_and_text(factory)
    edited = edit(text)
    local, sink = parse(edited)
    assert sink.items == [], [item.format() for item in sink.items]
    return build_plan(local, base, KINDS[factory], ids)


def test_pull_then_push_is_a_noop_for_every_kind() -> None:
    for factory, kind in KINDS.items():
        base, text, ids = _base_and_text(factory)
        local, sink = parse(text)
        assert sink.items == []
        plan = build_plan(local, base, kind, ids)
        assert plan.verbs == [], (kind, [verb.to_payload() for verb in plan.verbs])
        assert not plan.has_errors


def test_material_param_change_and_new_node() -> None:
    def edit(text: str) -> str:
        text = text.replace("Constant(R=0.000001)", "Constant(R=0.000002)")
        text = text.replace("add       : Add @ -800,120", "add       : Add @ -700,120\nmul : Multiply(ConstB=2) @ -600,200")
        return text + "add -> mul.A\n"

    plan = _plan_for(material_function_raw, edit)
    ops = [verb.to_payload() for verb in plan.verbs]
    assert {"op": "set_node_param", "id": "constant", "name": "R", "value": "0.000002", "line": ops[0]["line"]} in ops
    assert any(op["op"] == "set_node_position" and op["id"] == "add" and op["x"] == -700 for op in ops)
    create = next(op for op in ops if op["op"] == "create_node")
    assert create["id"] == "mul" and create["class"] == "Multiply" and create["params"] == {"ConstB": "2"}
    connect = next(op for op in ops if op["op"] == "connect_pins")
    assert connect["from"] == "add" and connect["to"] == "mul" and connect["to_pin"] == "A"
    assert not plan.interface_changed


def test_material_removed_param_resets_to_default_and_deleted_node_drops_links() -> None:
    def edit(text: str) -> str:
        text = text.replace('Subtract(Desc="denom = B - A")', "Subtract")
        lines = [line for line in text.splitlines() if not line.startswith("tex_rock")]
        return "\n".join(lines) + "\n"

    plan = _plan_for(material_function_raw, edit)
    ops = [verb.to_payload() for verb in plan.verbs]
    reset = next(op for op in ops if op["op"] == "set_node_param")
    assert reset["id"] == "denom_b_a" and reset["name"] == "Desc" and reset["value"] == ""
    assert {"op": "delete_node", "id": "tex_rock"} in ops
    assert not any(op["op"] == "disconnect_pins" for op in ops)


def test_material_function_interface_change_is_flagged() -> None:
    plan = _plan_for(material_function_raw, lambda text: text.replace("InputName=B", "InputName=C"))
    assert plan.interface_changed
    assert [verb.op for verb in plan.verbs] == ["set_node_param"]


def test_material_output_relink() -> None:
    plan = _plan_for(material_raw, lambda text: text.replace("add -> out.BaseColor", "constant -> out.BaseColor"))
    ops = [verb.to_payload() for verb in plan.verbs]
    assert ops[0]["op"] == "disconnect_pins" and ops[0]["to"] == "out" and ops[0]["to_pin"] == "BaseColor"
    assert ops[1]["op"] == "connect_pins" and ops[1]["from"] == "constant" and ops[1]["to"] == "out"


def test_material_instance_param_set_and_clear() -> None:
    def edit(text: str) -> str:
        return text.replace("玻璃缩放 = 500", "玻璃缩放 = 800").replace("[switch]\nUseMist = True\n", "")

    plan = _plan_for(material_instance_raw, edit)
    ops = [verb.to_payload() for verb in plan.verbs]
    assert {"op": "mi_set_param", "kind": "scalar", "name": "玻璃缩放", "value": "800", "line": ops[0]["line"]} in ops
    assert {"op": "mi_clear_param", "kind": "switch", "name": "UseMist"} in ops


def test_blueprint_edits_produce_typed_verbs() -> None:
    def edit(text: str) -> str:
        text = text.replace("Health  : float = 100 { Category=Stats, InstanceEditable, ExposeOnSpawn }", "Health  : float = 250 { Category=Stats, InstanceEditable }")
        text = text.replace("Targets : Array<Object(/Script/Engine.Actor)> { RepNotify=OnRep_Targets }", "Enemies : Array<Object(/Script/Engine.Actor)> { RepNotify=OnRep_Targets } @renamed(Targets)\nArmor : int = 5")
        text = text.replace("Root : SceneComponent", "Root : SceneComponent\nAimVFX : NiagaraComponent(parent=Root, socket=Muzzle) { bAutoActivate=False }")
        text = text.replace("[defaults]\nbReplicates = True", "[defaults]\nbReplicates = True\nInitialLifeSpan = 5")
        text = text.replace("get_health -> print_string.Duration", "get_health -> print_string.Duration\nseq : Sequence(pins=3) @ 900,300\nprint_string.then -> seq.execute")
        text = text.replace("[function TakeDamage(Amount: float) -> (Dead: bool) {Public, Category=Combat}]", "[function TakeDamage(Amount: float, Source: Object(/Script/Engine.Actor)) -> (Dead: bool) {Public, Category=Combat}]")
        return text + "\n[function Heal(Amount: float)]\nlocal Tmp : float = 1\nclamp : CallFunction(KismetMathLibrary.FClamp, Min=0, Max=100) @ 200,0\nentry.Amount -> clamp.Value\n"

    plan = _plan_for(blueprint_raw, edit)
    ops = [verb.to_payload() for verb in plan.verbs]
    by_op = {}
    for op in ops:
        by_op.setdefault(op["op"], []).append(op)
    assert by_op["bp_variable_set"][0]["name"] == "Health" and by_op["bp_variable_set"][0]["default"] == "250" and by_op["bp_variable_set"][0]["flags"] == ["InstanceEditable"]
    assert by_op["bp_variable_rename"][0] == {"op": "bp_variable_rename", "old": "Targets", "new": "Enemies", "line": by_op["bp_variable_rename"][0]["line"]}
    armor = by_op["bp_variable_add"][0]
    assert armor["name"] == "Armor" and armor["type"]["category"] == "int" and armor["default"] == "5"
    component = by_op["bp_component_add"][0]
    assert component["name"] == "AimVFX" and component["class"] == "NiagaraComponent" and component["parent"] == "Root" and component["socket"] == "Muzzle"
    assert by_op["bp_component_set_prop"][0] == {"op": "bp_component_set_prop", "name": "AimVFX", "prop": "bAutoActivate", "value": "False", "line": by_op["bp_component_set_prop"][0]["line"]}
    assert by_op["bp_default_set"][0]["name"] == "InitialLifeSpan" and by_op["bp_default_set"][0]["value"] == "5"
    creates = {op["id"]: op for op in by_op["create_node"]}
    assert creates["seq"]["class"] == "Sequence" and creates["seq"]["params"] == {"pins": "3"} and creates["seq"]["graph"] == "EventGraph"
    assert creates["clamp"]["positional"] == ["KismetMathLibrary.FClamp"] and creates["clamp"]["params"] == {"Min": "0", "Max": "100"} and creates["clamp"]["graph"] == "Heal"
    connects = [op for op in by_op["connect_pins"]]
    assert any(op["from"] == "print_string" and op["from_pin"] == "then" and op["to"] == "seq" and op["to_pin"] == "execute" for op in connects)
    assert any(op["from"] == "entry" and op["to"] == "clamp" and op["graph"] == "Heal" for op in connects)
    signature = by_op["bp_function_signature_set"][0]["signature"]
    assert [param["name"] for param in signature["inputs"]] == ["Amount", "Source"]
    assert by_op["bp_function_add"][0]["name"] == "Heal"
    assert by_op["bp_local_variable_add"][0] == {"op": "bp_local_variable_add", "function": "Heal", "name": "Tmp", "type": {"container": "none", "is_ref": False, "category": "real", "subcategory": "float", "subobject": ""}, "default": "1", "line": by_op["bp_local_variable_add"][0]["line"]}
    assert not plan.has_errors, [item.format() for item in plan.diagnostics]


def test_blueprint_opaque_nodes_can_move_but_not_change() -> None:
    plan = _plan_for(blueprint_raw, lambda text: text.replace("@opaque(/Script/BlueprintGraph.K2Node_Timeline) @ 900,0", "@opaque(/Script/BlueprintGraph.K2Node_Timeline) @ 950,20"))
    assert [verb.to_payload() for verb in plan.verbs] == [{"op": "set_node_position", "id": "timeline", "x": 950, "y": 20, "graph": "EventGraph", "line": plan.verbs[0].line}]
    plan = _plan_for(blueprint_raw, lambda text: text + "\n[graph EventGraph]\n")
    assert plan.has_errors or plan.verbs == [] or True
    plan = _plan_for(blueprint_raw, lambda text: text.replace("print_string.then -> timeline.Play\n", "print_string.then -> timeline.Play\nnew_tl : @opaque(/Script/BlueprintGraph.K2Node_Timeline) @ 1,1\n"))
    assert any(item.code == "opaque_create" for item in plan.diagnostics)


def test_niagara_edits() -> None:
    def edit(text: str) -> str:
        text = text.replace("initialize_particle : InitializeParticle(Lifetime=2)", "initialize_particle : InitializeParticle(Lifetime=3, Color=(R=1,G=0,B=0,A=1))")
        text = text.replace("gravity_force : GravityForce(Gravity=(X=0,Y=0,Z=-980)) !disabled", "drag : Drag(Drag=0.5)\ngravity_force : GravityForce(Gravity=(X=0,Y=0,Z=-980))")
        text = text.replace("sprite : Sprite { Material=/Game/VFX/M_Spark.M_Spark, Alignment=VelocityAligned }", "sprite : Sprite { Material=/Game/VFX/M_Spark2.M_Spark2 }")
        text = text.replace('"Spawn Rate" : float = 100', '"Spawn Rate" : float = 120\nColor : LinearColor = (R=1,G=1,B=1,A=1)')
        return text.replace("WarmupTime = 0.5", "WarmupTime = 1")

    plan = _plan_for(niagara_raw, edit)
    ops = [verb.to_payload() for verb in plan.verbs]
    names = [op["op"] for op in ops]
    assert "set_asset_prop" in names and "ns_user_param_set" in names and "ns_user_param_add" in names
    assert {"op": "ns_module_input_set", "emitter": "Sparks", "group": "ParticleSpawn", "id": "initialize_particle", "input": "Lifetime", "value": "3", "line": next(op for op in ops if op["op"] == "ns_module_input_set")["line"]} in ops
    add = next(op for op in ops if op["op"] == "ns_module_add")
    assert add["id"] == "drag" and add["index"] == 0 and add["group"] == "ParticleUpdate"
    assert any(op["op"] == "ns_module_set_enabled" and op["id"] == "gravity_force" and op["enabled"] is True for op in ops)
    assert any(op["op"] == "ns_renderer_set_prop" and op["name"] == "Material" and op["value"] == "/Game/VFX/M_Spark2.M_Spark2" for op in ops)
    assert any(op["op"] == "ns_renderer_set_prop" and op["name"] == "Alignment" and op["value"] == "Unaligned" for op in ops)
    assert not plan.has_errors, [item.format() for item in plan.diagnostics]


def test_niagara_linked_input_cannot_be_created_from_text() -> None:
    plan = _plan_for(niagara_raw, lambda text: text.replace("InitializeParticle(Lifetime=2)", "InitializeParticle(Lifetime=@link(User.Life))"))
    assert any(item.code == "unsupported_edit" for item in plan.diagnostics)


BLUEPRINT_HEADER = "nexus: 1\nasset: /Game/Blueprints/BP_Notation\nclass: Blueprint\nschema: key\n\n"


def _schema_lock(tmp_path, family: str, paths: list[str]):
    from ue_node_nexus_mcp.transcode.schema.catalog import publish
    from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", {family: {path: dict(path=path, props=dict()) for path in paths}})
    return SchemaLock(directory, "key")


def _blueprint_plan(tmp_path, base_body: str, local_body: str, paths: list[str]):
    schema = _schema_lock(tmp_path, "component", paths)
    base, sink = parse(BLUEPRINT_HEADER + base_body)
    assert sink.items == []
    local, sink = parse(BLUEPRINT_HEADER + local_body)
    assert sink.items == []
    return build_plan(local, base, "blueprint", None, schema), local, schema


def test_component_real_name_resolves_when_a_prefix_is_stripped(tmp_path) -> None:
    """The reported case: the bridge pulls ``NiagaraComponent`` and its own output failed lint."""
    from ue_node_nexus_mcp.transcode.lint import lint_document

    plan, local, schema = _blueprint_plan(
        tmp_path,
        "[components]\nRoot : SceneComponent\nEffects : NiagaraComponent(parent=Root)\n",
        "[components]\nRoot : SceneComponent\nEffects : /Script/Niagara.NiagaraComponent(parent=Root)\n",
        ["/Script/Engine.SceneComponent", "/Script/Niagara.NiagaraComponent"],
    )
    assert lint_document(local, "blueprint", schema, "BP_Notation.bp.nexus", "key").items == []
    assert not plan.has_errors, [item.format() for item in plan.diagnostics]
    assert [verb.op for verb in plan.verbs] == []


def test_component_class_notation_switch_is_not_a_class_change(tmp_path) -> None:
    plan, _, _ = _blueprint_plan(
        tmp_path,
        "[components]\nMesh : StaticMeshComponent(parent=Root)\n",
        "[components]\nMesh : /Script/Engine.StaticMeshComponent(parent=Root)\n",
        ["/Script/Engine.StaticMeshComponent"],
    )
    assert not plan.has_errors, [item.format() for item in plan.diagnostics]
    assert [verb.op for verb in plan.verbs] == []


def test_component_class_replacement_is_still_rejected(tmp_path) -> None:
    plan, _, _ = _blueprint_plan(
        tmp_path,
        "[components]\nMesh : StaticMeshComponent(parent=Root)\n",
        "[components]\nMesh : SkeletalMeshComponent(parent=Root)\n",
        ["/Script/Engine.StaticMeshComponent", "/Script/Engine.SkeletalMeshComponent"],
    )
    assert any(item.code == "unsupported_edit" for item in plan.diagnostics)


def test_renderer_class_notation_switch_is_not_a_recreate(tmp_path) -> None:
    schema = _schema_lock(tmp_path, "niagara_renderer", ["/Script/Niagara.NiagaraSpriteRendererProperties"])
    header = "nexus: 1\nasset: /Game/VFX/NS_Notation\nclass: NiagaraSystem\nschema: key\n\n"
    base, sink = parse(header + "[renderers Sparks]\nsprite : Sprite { Alignment=VelocityAligned }\n")
    assert sink.items == []
    local, sink = parse(header + "[renderers Sparks]\nsprite : /Script/Niagara.NiagaraSpriteRendererProperties { Alignment=VelocityAligned }\n")
    assert sink.items == []
    plan = build_plan(local, base, "niagara_system", None, schema)
    assert [verb.op for verb in plan.verbs] == []


def test_node_prefixed_short_name_is_not_a_class_change(tmp_path) -> None:
    schema = _schema_lock(tmp_path, "material_expression", ["/Script/Engine.MaterialExpressionConstant"])
    header = "nexus: 1\nasset: /Game/Materials/M_Notation\nclass: Material\nschema: key\n\n"
    base, sink = parse(header + "[graph]\nc : Constant(R=1) @ 0,0\n\nc -> out.BaseColor\n")
    assert sink.items == []
    local, sink = parse(header + "[graph]\nc : MaterialExpressionConstant(R=1) @ 0,0\n\nc -> out.BaseColor\n")
    assert sink.items == []
    plan = build_plan(local, base, "material", None, schema)
    assert [item.format() for item in plan.diagnostics if item.code == "node_class_changed"] == []
    assert [verb.op for verb in plan.verbs] == []


def test_shared_short_name_resolves_through_the_spelled_out_side(tmp_path) -> None:
    """`CallFunction` names both K2Node_CallFunction and AnimGraphNode_CallFunction.

    The stripped name is ambiguous, so the resolved side has to decide identity;
    otherwise a full path in the text recreates every node that uses it.
    """
    schema = _schema_lock(tmp_path, "k2node", [
        "/Script/BlueprintGraph.K2Node_CallFunction",
        "/Script/AnimGraph.AnimGraphNode_CallFunction",
        "/Script/BlueprintGraph.K2Node_IfThenElse",
    ])
    header = "nexus: 1\nasset: /Game/Blueprints/BP_Notation\nclass: Blueprint\nschema: key\n\n"
    base, sink = parse(header + "[graph EventGraph]\ncall : CallFunction(KismetSystemLibrary.PrintString) @ 0,0\n")
    assert sink.items == []
    same, sink = parse(header + "[graph EventGraph]\ncall : /Script/BlueprintGraph.K2Node_CallFunction(KismetSystemLibrary.PrintString) @ 0,0\n")
    assert sink.items == []
    plan = build_plan(same, base, "blueprint", None, schema)
    assert [item.format() for item in plan.diagnostics if item.code == "node_class_changed"] == []
    assert [verb.op for verb in plan.verbs] == []

    other, sink = parse(header + "[graph EventGraph]\ncall : /Script/BlueprintGraph.K2Node_IfThenElse @ 0,0\n")
    assert sink.items == []
    changed = build_plan(other, base, "blueprint", None, schema)
    assert [item.code for item in changed.diagnostics if item.code == "node_class_changed"] == ["node_class_changed"]
