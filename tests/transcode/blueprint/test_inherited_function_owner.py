"""An inherited call owner is one function, so it has one spelling.

UE binds ``PlayerController.GetViewTarget`` to the UFunction declared on
``Controller`` and exports that owner after compile. Authored and observed
states must both reach the declaring class, or the isolated readback rejects
a publication that did exactly what was asked.
"""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.raw.nodes import node_decl
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.functions import reference
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse
from tests.support.semantics import receipt_workspace

VIEW_TARGET = "/Script/Engine.Controller.GetViewTarget"
HUD = "/Script/Engine.PlayerController.GetHUD"
FIELD = "/Game/Leaves/BP_Field.BP_Field_C.GetDensity"


@pytest.fixture
def schema(tmp_path):
    classes = dict((path, dict(path=path, name=path.rsplit(".", 1)[-1], inheritance=parent))
                   for path, parent in (("/Script/Engine.Actor", ""),
                                        ("/Script/Engine.Controller", "/Script/Engine.Actor"),
                                        ("/Script/Engine.PlayerController", "/Script/Engine.Controller"),
                                        ("/Game/Leaves/BP_Field.BP_Field_C", "/Script/Engine.Actor")))
    functions = dict((path, dict(path=path, owner=path.rsplit(".", 1)[0], params=[])) for path in (VIEW_TARGET, HUD, FIELD))
    directory = tmp_path / "schema"
    publish(directory, "key", dict(asset=classes, callable_functions=functions,
                                   k2node={"/Script/BlueprintGraph.K2Node_CallFunction": dict(dynamic_pins=True)}))
    return SchemaLock(directory, "key")


def text(call):
    return ("nexus: 1\nasset: /Game/BFL_Camera\nclass: Blueprint\nschema: key\n"
            "\n[graph EventGraph]\ntarget : CallFunction(" + call + ") @ 0,0\n")


def positional(call, schema):
    document, sink = parse(text(call))
    assert not sink.has_errors
    semantic, _, _ = encode(document, "blueprint", None, "test", schema)
    entity = next(iter(semantic["sections"]["graph:EventGraph"]["entities"].values()))
    return entity["positional"][0]["value"]


def call_raw(owner, name):
    node = dict(guid="TARGET", class_short="CallFunction", supported=True, x=0, y=0, props=[], pins=[],
                config=dict(function_owner=owner, function_name=name, self_context="false"))
    return dict(raw_version=1, asset_path="/Game/BFL_Camera.BFL_Camera", kind="blueprint", schema_key="key",
                class_short="Blueprint", props=[], blueprint=dict(graphs=[dict(name="EventGraph", nodes=[node])]))


@pytest.mark.parametrize("call", ["PlayerController.GetViewTarget", "Controller.GetViewTarget",
                                  "/Script/Engine.PlayerController.GetViewTarget", VIEW_TARGET])
def test_every_spelling_of_an_inherited_call_encodes_to_the_declaring_class(schema, call):
    assert positional(call, schema) == "Controller.GetViewTarget"


def test_native_export_names_the_same_declaring_class(schema):
    node = call_raw("/Script/Engine.Controller", "GetViewTarget")["blueprint"]["graphs"][0]["nodes"][0]
    node["class"] = "/Script/BlueprintGraph.K2Node_CallFunction"
    assert node_decl("target", node, schema).positional() == ["Controller.GetViewTarget"]


def test_function_declared_on_the_subclass_keeps_its_owner(schema):
    assert positional("PlayerController.GetHUD", schema) == "PlayerController.GetHUD"


@pytest.mark.parametrize("call", ["BP_Field_C.GetDensity", FIELD])
def test_blueprint_function_uses_its_full_declaring_path(schema, call):
    assert positional(call, schema) == FIELD


def test_short_blueprint_owner_accepts_only_its_actual_function_readback(tmp_path, schema):
    candidate = capture(text("BP_Field_C.GetDensity"), None, "author", "blueprint", schema)
    workspace, commit, asset = receipt_workspace(tmp_path / "store", candidate)
    workspace.schema = schema
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="create_node", id="target", graph="EventGraph")]),
                  receipt=dict(after=call_raw(FIELD.rpartition(".")[0], "GetDensity"),
                               response_data=dict(id_map=dict(target="TARGET"))))
    assert actual_snapshot(workspace, record)
    record["receipt"]["after"] = call_raw("/Game/Other/BP_Field.BP_Field_C", "GetDensity")
    with pytest.raises(SyncError) as failure:
        actual_snapshot(workspace, record)
    assert failure.value.code == "apply_result_mismatch"


def test_unresolved_and_self_owners_keep_their_authored_spelling(schema):
    assert reference(schema, "PlayerController", "Missing") == "PlayerController.Missing"
    assert reference(schema, "self", "GetViewTarget") == "self.GetViewTarget"
    assert reference(schema, "/Game/BP_Owner.BP_Owner_C", "Run") == "/Game/BP_Owner.BP_Owner_C.Run"
    assert reference(None, "PlayerController", "GetViewTarget") == "PlayerController.GetViewTarget"


def test_isolated_readback_accepts_the_inherited_spelling_and_rejects_another_function(tmp_path, schema):
    candidate = capture(text("PlayerController.GetViewTarget"), None, "author", "blueprint", schema)
    workspace, commit, asset = receipt_workspace(tmp_path / "store", candidate)
    workspace.schema = schema
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="create_node", id="target", graph="EventGraph")]),
                  receipt=dict(after=call_raw("/Script/Engine.Controller", "GetViewTarget"),
                               response_data=dict(id_map=dict(target="TARGET"))))
    actual = actual_snapshot(workspace, record)
    entity = next(iter(actual["semantic"]["sections"]["graph:EventGraph"]["entities"].values()))
    assert entity["positional"][0]["value"] == "Controller.GetViewTarget"
    altered = deepcopy(record)
    altered["receipt"]["after"] = call_raw("/Script/Engine.PlayerController", "GetHUD")
    with pytest.raises(SyncError) as error:
        actual_snapshot(workspace, altered)
    assert error.value.code == "apply_result_mismatch"
