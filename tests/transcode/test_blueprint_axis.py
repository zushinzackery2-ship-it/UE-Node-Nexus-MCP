"""Native Blueprint paths and node replacements keep their graph contract."""

from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.paths import object_path, package_name
from ue_node_nexus_mcp.transcode.sync_deps import document_dependencies


HEADER = "nexus: 1\nasset: /Game/Blueprints/BP_CameraFollow.BP_CameraFollow\nclass: Blueprint\nschema: test\n"


def test_blueprint_class_method_reference_keeps_its_package_boundary():
    reference = "/Game/Blueprints/BP_CameraFollow.BP_CameraFollow_C.Update"
    assert package_name(reference) == "/Game/Blueprints/BP_CameraFollow"
    assert object_path(reference) == "/Game/Blueprints/BP_CameraFollow.BP_CameraFollow"
    document, sink = parse(HEADER + "\n[graph EventGraph]\ncall : CallFunction(" + reference + ")\n")
    assert not sink.has_errors
    assert document_dependencies(document, "blueprint") == set()

    external = "/Game/Blueprints/BP_Other.BP_Other_C.Update"
    document, sink = parse(HEADER + "\n[graph EventGraph]\ncall : CallFunction(" + external + ")\n")
    assert not sink.has_errors
    assert document_dependencies(document, "blueprint") == {"/Game/Blueprints/BP_Other.BP_Other"}


def test_replacing_axis_event_restores_unchanged_outgoing_connection():
    base, sink = parse(HEADER + "\n[graph EventGraph]\naxis : InputAxisEvent(MouseX)\nsink : CallFunction(KismetSystemLibrary.PrintString)\naxis.then -> sink.execute\n")
    assert not sink.has_errors
    local, sink = parse(HEADER + "\n[graph EventGraph]\naxis : InputAxisEvent(MouseY)\nsink : CallFunction(KismetSystemLibrary.PrintString)\naxis.then -> sink.execute\n")
    assert not sink.has_errors

    plan = build_plan(local, base, "blueprint", {"axis": "AXIS", "sink": "SINK"})
    assert [(verb.op, verb.args.get("id")) for verb in plan.verbs if verb.op in ("delete_node", "create_node")] == [
        ("delete_node", "axis"), ("create_node", "axis")]
    assert [(verb.args["from"], verb.args["to"]) for verb in plan.verbs if verb.op == "connect_pins"] == [("axis", "sink")]


def test_replacing_middle_node_restores_both_unchanged_edges():
    edges = "start.then -> middle.execute\nmiddle.then -> end.execute\n"
    nodes = "start : Event(Actor.ReceiveBeginPlay)\nmiddle : CallFunction({})\nend : CallFunction(KismetSystemLibrary.PrintString)\n"
    base = parse(HEADER + "\n[graph EventGraph]\n" + nodes.format("KismetSystemLibrary.PrintString") + edges)[0]
    local = parse(HEADER + "\n[graph EventGraph]\n" + nodes.format("KismetSystemLibrary.PrintWarning") + edges)[0]

    plan = build_plan(local, base, "blueprint", {"start": "START", "middle": "MIDDLE", "end": "END"})
    assert [(verb.args["from"], verb.args["to"]) for verb in plan.verbs if verb.op == "connect_pins"] == [
        ("start", "middle"), ("middle", "end")]
