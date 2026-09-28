"""Native Blueprint spellings must agree with the authored writable state."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.merge.engine import Comparison
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw, text_of
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.support.semantics import receipt_workspace, self_graph_raw

from tests.support.blueprint import branch_raw


@pytest.mark.parametrize("reference", ["Actor.K2_DestroyActor", "/Script/Engine.Actor.K2_DestroyActor"])
def test_native_function_path_and_exported_owner_label_are_equivalent(reference):
    canonical = from_raw(self_graph_raw())
    prefix, call = text_of(canonical).split("CallFunction(", 1)
    _, suffix = call.split(")", 1)
    text = prefix + "CallFunction(" + reference + ")" + suffix
    authored = capture(text, canonical, "author", "blueprint")
    assert authored["semantic"] == canonical["semantic"]


@pytest.mark.parametrize("self_context", ["true", "false"])
def test_another_native_owner_is_a_semantic_change(self_context):
    raw = self_graph_raw()
    raw["blueprint"]["graphs"][0]["nodes"][1]["config"]["self_context"] = self_context
    canonical = from_raw(raw)
    prefix, call = text_of(canonical).split("CallFunction(", 1)
    _, suffix = call.split(")", 1)
    text = prefix + "CallFunction(/Script/OtherPlugin.Actor.K2_DestroyActor)" + suffix
    authored = capture(text, canonical, "author", "blueprint")
    assert authored["semantic"] != canonical["semantic"]


def test_explicit_generated_input_default_has_the_same_semantics():
    canonical = from_raw(branch_raw())
    text = text_of(canonical).replace("IfThenElse", "Branch(Condition=True)")
    authored = capture(text, canonical, "author", "blueprint")
    assert authored["semantic"] == canonical["semantic"]


def test_first_blueprint_readback_accepts_generated_defaults(tmp_path):
    raw = branch_raw()
    candidate = capture("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
                        "[graph EventGraph]\nsplit : Branch(Condition=True) @ 0,0\n", None, "author", "blueprint")
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="create_node", id="split", graph="EventGraph")]),
                  receipt=dict(after=raw, response_data=dict(id_map=dict(split="BRANCH"))))
    assert actual_snapshot(workspace, record)
    altered = deepcopy(record)
    altered["receipt"]["after"]["blueprint"]["graphs"][0]["nodes"][0]["pins"][0]["default"] = "False"
    with pytest.raises(SyncError) as error:
        actual_snapshot(workspace, altered)
    assert error.value.code == "apply_result_mismatch"


def test_local_variable_export_keeps_a_reference_in_its_function_scope():
    raw = self_graph_raw()
    raw["blueprint"]["graphs"] = []
    for name in ("First", "Second"):
        pin_type = dict(category="real", subcategory="float")
        node = dict(guid=name, class_short="VariableGet", supported=True, x=0, y=0, props=[], pins=[],
                    config=dict(variable_name="cache", variable_owner="/Game/BP_Test.SKEL_BP_Test_C",
                                variable_scope=name, self_context="false"))
        graph = dict(name=name, kind="function", nodes=[node],
                     signature=dict(locals=[dict(name="cache", type=pin_type, default="0.338")]))
        raw["blueprint"]["graphs"].append(graph)
    snapshot = from_raw(raw)
    references = []
    for section in snapshot["semantic"]["sections"].values():
        if section["name"] != "function":
            continue
        local = next(key for key, value in section["entities"].items() if value["modifier"] == "local")
        get = next(value for value in section["entities"].values() if value["type"] == "VariableGet")
        assert get["positional"][0]["ref"] == local
        references.append(local)
    assert len(set(references)) == 2
    assert "SKEL_" not in text_of(snapshot)


@pytest.mark.parametrize("body", ["[function MissingParen]\n", "[dispatchers]\nInvalid @category=Events\n"])
def test_malformed_signature_has_a_structured_source_location(body):
    with pytest.raises(SyncError) as error:
        capture("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n" + body, None, "author", "blueprint")
    assert error.value.code == "invalid_signature"
    assert error.value.details["line"] >= 4
    assert error.value.details["col"] >= 1


def test_duplicate_validation_conflict_updates_its_own_location():
    compare = Comparison("/Game/BP_Test.BP_Test")
    compare.conflict(["first"], 0, 1, 2)
    compare.conflict(["second"], 0, 1, 2, location=dict(line=20))
    compare.conflict(["first"], 0, 1, 2, location=dict(line=5, col=3))
    assert len(compare.conflicts) == 2
    assert compare.conflicts[0]["line"] == 5
    assert compare.conflicts[1]["line"] == 20


@pytest.mark.parametrize("kind,default", [("float", "0"), ("bool", "false"), ("string", ""),
                                        ("Struct(/Script/CoreUObject.Vector)", "0, 0, 0")])
def test_function_signature_uses_the_native_generated_default(kind, default):
    from ue_node_nexus_mcp.transcode.blueprint.types import parse_type_text
    from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot

    raw = self_graph_raw()
    parameter = dict(name="Result", type=parse_type_text(kind), default=default, autogenerated=default)
    raw["blueprint"]["graphs"] = [dict(name="Query", kind="function", nodes=[],
                                        signature=dict(inputs=[], outputs=[parameter]))]
    canonical = from_raw(raw)
    authored = capture("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
                       f"[function Query() -> (Result: {kind})]\n", None, "author", "blueprint")
    normalized = normalize_snapshot(authored, canonical)
    assert normalized["semantic"]["sections"]["function:Query"]["args"] == canonical["semantic"]["sections"]["function:Query"]["args"]


def test_function_signature_keeps_an_actual_non_default_edit():
    from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot

    raw = self_graph_raw()
    parameter = dict(name="Result", type=dict(category="real", subcategory="float"), default="1", autogenerated="0")
    raw["blueprint"]["graphs"] = [dict(name="Query", kind="function", nodes=[],
                                        signature=dict(inputs=[], outputs=[parameter]))]
    canonical = from_raw(raw)
    authored = capture("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\n"
                       "[function Query() -> (Result: float)]\n", None, "author", "blueprint")
    normalized = normalize_snapshot(authored, canonical)
    assert normalized["semantic"]["sections"]["function:Query"]["args"] != canonical["semantic"]["sections"]["function:Query"]["args"]
