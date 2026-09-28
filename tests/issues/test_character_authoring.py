"""Character publication needs component order and Blueprint API dependencies."""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync_deps import order_assets
from ue_node_nexus_mcp.transcode.sync_project import SyncError


def document(name, body):
    result, sink = parse(f"nexus: 1\nasset: /Game/{name}\nclass: Blueprint\n" + body)
    assert not sink.errors()
    return result


def test_component_parent_is_created_before_child_regardless_of_text_order():
    local = document("Pawn", "[components]\nView : CameraComponent(parent=Boom, socket=SpringEndpoint)\n"
                     "Boom : SpringArmComponent(parent=Root)\nRoot : @inherited(CapsuleComponent)\n")
    base = document("Pawn", "[components]\nRoot : @inherited(CapsuleComponent)\n")
    plan = build_plan(local, base, "blueprint")
    assert not plan.has_errors
    additions = [verb.args["name"] for verb in plan.verbs if verb.op == "bp_component_add"]
    assert additions == ["Boom", "View"]


def test_component_cycles_fail_before_publication():
    local = document("Pawn", "[components]\nA : SceneComponent(parent=B)\nB : SceneComponent(parent=A)\n")
    plan = build_plan(local, None, "blueprint")
    assert any(item.code == "component_cycle" for item in plan.diagnostics)


def test_added_function_in_existing_blueprint_orders_provider_before_consumer():
    base = document("ZMotor", "[function Initialize()]\n")
    provider = document("ZMotor", "[function Initialize()]\n[function Move()]\n")
    consumer = document("APawn", "[graph EventGraph]\nmove : CallFunction(/Game/ZMotor.ZMotor_C.Move) @ 0,0\n")
    plan = build_plan(provider, base, "blueprint")
    assert plan.interface_changed
    documents = dict((("/Game/APawn.APawn", ("blueprint", consumer)),
                      ("/Game/ZMotor.ZMotor", ("blueprint", provider))))
    assert order_assets(documents, set((plan.asset_path,))) == ["/Game/ZMotor.ZMotor", "/Game/APawn.APawn"]


def test_default_value_edit_does_not_create_an_api_dependency():
    base = document("Motor", "[variables]\nSpeed : float = 420\n")
    local = document("Motor", "[variables]\nSpeed : float = 650\n")
    assert not build_plan(local, base, "blueprint").interface_changed


def input_source(connections):
    return (
        "nexus: 1\nasset: /Game/Pawn\nclass: Blueprint\n"
        "[function Release()]\n[graph EventGraph]\n"
        "jump : EnhancedInputAction(/Game/IA_Jump.IA_Jump) @ 0,0\n"
        "release : CallFunction(self.Release) @ 200,0\n"
        "cancel : CallFunction(self.Release) @ 200,200\n" + connections)


def test_enhanced_input_completion_and_cancel_share_one_execution_input():
    text = input_source("jump.Completed -> release.execute\njump.Canceled -> release.execute\n")
    snapshot = capture(text, None, "author", "blueprint")
    assert len(snapshot["semantic"]["sections"]["graph:EventGraph"]["links"]) == 2


def test_enhanced_input_execution_output_still_accepts_only_one_connection():
    text = input_source("jump.Completed -> release.execute\njump.Completed -> cancel.execute\n")
    with pytest.raises(SyncError, match="multiple connections"):
        capture(text, None, "author", "blueprint")
