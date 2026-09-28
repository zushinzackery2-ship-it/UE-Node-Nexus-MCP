"""Native metadata canonicalizes independently introduced declarations."""

from copy import deepcopy
import pytest

from ue_node_nexus_mcp.transcode.collaboration.merge.engine import merge_snapshots
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw, text_of
from tests.support.blueprint import branch_raw
from tests.support.semantics import self_graph_raw


@pytest.mark.parametrize("position", ["@ 0,0", ""])
def test_add_add_native_generated_defaults_preserve_actual_conflicts(position):
    authored = capture("nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
                       f"[graph EventGraph]\nsplit : Branch(Condition=True) {position}\n",
                       None, "author", "blueprint")
    raw = branch_raw()
    bound = deepcopy(authored)
    binding = next(iter(bound["bindings"].values()))
    binding["physical"] = "BRANCH"
    binding["meta"]["guid"] = "BRANCH"
    native = from_raw(raw, bound)
    result = merge_snapshots(None, authored, native)
    assert not result.conflicts
    wrong = deepcopy(raw)
    wrong["blueprint"]["graphs"][0]["nodes"][0]["pins"][0]["default"] = "False"
    assert merge_snapshots(None, authored, from_raw(wrong, bound)).conflicts
    moved = deepcopy(raw)
    moved["blueprint"]["graphs"][0]["nodes"][0]["x"] = 200
    assert bool(merge_snapshots(None, authored, from_raw(moved, bound)).conflicts) == bool(position)


def test_inherited_self_call_matches_native_owner_without_changing_other_function():
    raw = self_graph_raw()
    call = raw["blueprint"]["graphs"][0]["nodes"][1]
    call["config"] = dict(function_owner="/Script/Engine.AnimInstance",
                          function_name="GetOwningComponent", self_context="true")
    native = from_raw(raw)
    text = text_of(native)
    authored = capture(text.replace("self.GetOwningComponent", "AnimInstance.GetOwningComponent"),
                       native, "author", "blueprint")
    assert authored["semantic"] == native["semantic"]
    changed = capture(text.replace("GetOwningComponent", "GetOwningActor"), native, "author", "blueprint")
    assert changed["semantic"] != native["semantic"]
