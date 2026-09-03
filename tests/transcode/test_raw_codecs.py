from __future__ import annotations

import re

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse

from .fixtures import blueprint_raw, material_instance_raw, material_raw, material_function_raw, niagara_raw


def _round_trip(raw: dict) -> str:
    """Emit -> parse -> emit must be stable; returns text with id padding collapsed."""
    document, _, _ = document_from_raw(raw)
    text = emit(document)
    parsed, sink = parse(text)
    assert sink.items == [], [item.format() for item in sink.items]
    assert emit(parsed) == text
    return re.sub(r" {2,}", " ", text)


def test_material_function_text_shape() -> None:
    text = _round_trip(material_function_raw())
    assert "class: MaterialFunction" in text
    assert "asset: /Game/WaterStains/Functions/MF_WS_S" in text
    assert "Description = smoothstep" in text
    assert "bExposeToLibrary = True" in text
    assert "LibraryCategoriesText" not in text  # default value is dropped
    assert 'denom_b_a : Subtract(Desc="denom = B - A") @ -1000,80' in text
    assert "in_a" in text and "in_b" in text
    assert "constant : Constant(R=0.000001) @ -1000,220" in text
    assert "in_b -> denom_b_a.A" in text
    assert "in_a -> denom_b_a.B" in text
    assert "constant -> add.B" in text
    assert "tex_rock.R -> out" in text
    assert "ConstA" not in text


def test_material_ids_are_stable_across_repulls() -> None:
    raw = material_function_raw()
    document, ids, order = document_from_raw(raw)
    assert ids["G-SUB"] == "denom_b_a"
    raw["graph"]["nodes"][2]["props"][1]["value"] = "renamed"
    raw["graph"]["nodes"].append({**raw["graph"]["nodes"][3], "guid": "G-EPS2", "x": -1000, "y": 300})
    document2, ids2, order2 = document_from_raw(raw, ids, order)
    assert ids2["G-SUB"] == "denom_b_a"
    assert ids2["G-EPS2"] == "constant_2"
    graph_ids = [decl.id for decl in document2.section("graph").decls()]
    assert graph_ids[: len(order["graph"])] == order["graph"]
    assert graph_ids[-1] == "constant_2"


def test_material_outputs_use_implicit_out_node() -> None:
    text = _round_trip(material_raw())
    assert "BlendMode = BLEND_Translucent" in text
    assert "TwoSided" not in text
    assert "add -> out.BaseColor" in text
    assert "tex_rock.A -> out.Opacity" in text
    assert "\nout " not in text


def test_material_instance_sections() -> None:
    text = _round_trip(material_instance_raw())
    assert "Parent = /Game/Materials/M_Glass.M_Glass" in text
    assert "PhysMaterial" not in text
    assert "[scalar]\n玻璃缩放 = 500\n" in text
    assert "[vector]\nTint = (R=1,G=0.9,B=0.8,A=1)\n" in text
    assert "[switch]\nUseMist = True\n" in text


def test_blueprint_text_shape() -> None:
    text = _round_trip(blueprint_raw())
    assert "ParentClass = /Script/Engine.Actor" in text
    assert "Health : float = 100 { Category=Stats, InstanceEditable, ExposeOnSpawn }" in text
    assert "Targets : Array<Object(/Script/Engine.Actor)> { RepNotify=OnRep_Targets }" in text
    assert "Mesh : StaticMeshComponent(parent=Root) { StaticMesh=/Game/Meshes/SM_Door.SM_Door, RelativeLocation=(X=0,Y=0,Z=50) }" in text
    assert "[defaults]\nbReplicates = True\n" in text
    assert "OnOpened(By: Object(/Script/Engine.Actor))" in text
    assert "[graph EventGraph]" in text
    assert "ev_begin_play : Event(Actor.ReceiveBeginPlay) @ 0,0" in text
    assert "get_health : VariableGet(Health) @ 300,120" in text
    assert 'print_string : CallFunction(KismetSystemLibrary.PrintString) @ 600,0 @comment(debug)' in text
    assert "timeline : @opaque(/Script/BlueprintGraph.K2Node_Timeline) @ 900,0" in text
    assert "ev_begin_play.then -> print_string.execute" in text
    assert "get_health -> print_string.Duration" in text
    assert "print_string.then -> timeline.Play" in text
    assert "[function TakeDamage(Amount: float) -> (Dead: bool) {Public, Category=Combat}]" in text
    assert "local Remaining : float = 0" in text
    assert "less_equal_float_float : CallFunction(KismetMathLibrary.LessEqual_FloatFloat) @ 300,0" in text
    assert "FunctionResult" not in text and "FunctionEntry" not in text
    assert "entry.Amount -> less_equal_float_float.A" in text
    assert "entry.then -> result.execute" in text
    assert "less_equal_float_float -> result.Dead" in text


def test_niagara_text_shape() -> None:
    text = _round_trip(niagara_raw())
    assert "[user]\n\"Spawn Rate\" : float = 100\n" in text
    assert "[emitter Sparks]\nEnabled = True\nParent = /Game/VFX/Emitters/E_Base.E_Base\n" in text
    assert "SimTarget" not in text
    assert "[stack Sparks/EmitterSpawn]\n" in text
    assert 'spawn_rate : SpawnRate(SpawnRate=@link(User.Spawn Rate))' in text
    assert "initialize_particle : InitializeParticle(Lifetime=2)" in text
    assert "gravity_force : GravityForce(Gravity=(X=0,Y=0,Z=-980)) !disabled" in text
    assert "[stack Sparks/Event:Collision]\nopaque : @opaque(Event:Collision)" in text
    assert "[renderers Sparks]\nsprite : Sprite { Material=/Game/VFX/M_Spark.M_Spark, Alignment=VelocityAligned }" in text
