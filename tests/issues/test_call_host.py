"""Issue 10: a call to an array function must land on the class that types its pins.

The editor spawns ``K2Node_CallArrayFunction`` for an ``ArrayParm`` function; a
plain call node for it never resolves its wildcard pins and fails the compile
gate. The mirror names the call, ``CallFunction``, and the bridge picks the host,
so every host spelling of one call is one state, and a spelling that contradicts
the function is refused before anything reaches UE.
"""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.bp_call_host import call_spelling, required_host
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.diff_common import same_class
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

HOSTS = ("CallFunction", "CallArrayFunction", "CallDataTableFunction", "CallMaterialParameterCollectionFunction")
ARRAY_CLEAR = "/Script/Engine.KismetArrayLibrary.Array_Clear"
PRINT = "/Script/Engine.KismetSystemLibrary.PrintString"


def lock(tmp_path) -> SchemaLock:
    directory = tmp_path / ".nexus" / "schema" / "key"
    k2node = dict((f"/Script/BlueprintGraph.K2Node_{name}", dict(path=f"/Script/BlueprintGraph.K2Node_{name}", props=dict())) for name in HOSTS)
    functions = {
        ARRAY_CLEAR: dict(path=ARRAY_CLEAR, node_class="K2Node_CallArrayFunction", params=[dict(name="TargetArray")]),
        PRINT: dict(path=PRINT, node_class="K2Node_CallFunction", params=[dict(name="InString")]),
    }
    publish(directory, "key", dict(k2node=k2node, functions=functions))
    return SchemaLock(directory, "key")


def blueprint(node: str) -> str:
    return ("nexus: 1\nasset: /Game/Blueprints/BP_Call\nclass: Blueprint\nschema: key\n\n"
            f"[graph EventGraph]\nclear : {node} @ 0,0\n")


def codes(tmp_path, node: str) -> list[str]:
    document, sink = parse(blueprint(node))
    assert sink.items == []
    return [item.code for item in lint_document(document, "blueprint", lock(tmp_path), "BP_Call.bp.nexus").errors()]


def test_every_host_spelling_is_the_same_call():
    for name in HOSTS:
        assert call_spelling(name) == "CallFunction"
        assert call_spelling("K2Node_" + name) == "CallFunction"
        assert call_spelling("/Script/BlueprintGraph.K2Node_" + name) == "CallFunction"
    assert call_spelling("CallParentFunction") == "CallParentFunction"
    assert call_spelling("MacroInstance") == "MacroInstance"
    assert required_host(dict(node_class="K2Node_CallArrayFunction")) == "CallArrayFunction"
    assert required_host(dict()) is None


def test_a_host_spelling_is_not_a_class_change():
    assert same_class(None, "k2node", "CallFunction", "CallArrayFunction")
    assert same_class(None, "k2node", "/Script/BlueprintGraph.K2Node_CallArrayFunction", "CallFunction")
    # A node that is not a call is still a different class.
    assert not same_class(None, "k2node", "CallFunction", "CallParentFunction")
    # The spellings are Blueprint node classes; another family keeps its names.
    assert not same_class(None, "material_expression", "CallFunction", "CallArrayFunction")


def test_the_mirror_state_of_a_call_does_not_depend_on_its_spelling():
    states = []
    for name in ("CallFunction", "CallArrayFunction"):
        document, _ = parse(blueprint(f"{name}(KismetArrayLibrary.Array_Clear)"))
        semantic, _, _ = encode(document, "blueprint", None, "test")
        entity = next(iter(semantic["sections"].values()))["entities"]
        states.append([item["type"] for item in entity.values()])
    assert states == [["CallFunction"], ["CallFunction"]]


def test_the_plain_call_to_an_array_function_lints_clean(tmp_path):
    assert codes(tmp_path, "CallFunction(KismetArrayLibrary.Array_Clear)") == []
    assert codes(tmp_path, "CallArrayFunction(KismetArrayLibrary.Array_Clear)") == []


def test_a_spelling_that_contradicts_the_function_is_refused(tmp_path):
    assert codes(tmp_path, "CallArrayFunction(KismetSystemLibrary.PrintString)") == ["node_class_mismatch"]
    assert codes(tmp_path, "CallDataTableFunction(KismetArrayLibrary.Array_Clear)") == ["node_class_mismatch"]
    assert codes(tmp_path, "CallArrayFunction(self.Refresh)") == ["node_class_mismatch"]


def test_switching_spellings_plans_nothing_and_a_wrong_host_is_recreated(tmp_path):
    schema = lock(tmp_path)
    plain, _ = parse(blueprint("CallFunction(KismetArrayLibrary.Array_Clear)"))
    spelled, _ = parse(blueprint("CallArrayFunction(KismetArrayLibrary.Array_Clear)"))
    assert build_plan(spelled, plain, "blueprint", dict(), schema).verbs == []
    # An older bridge built a plain call node for the array function. The export
    # marks it opaque, so stating the call again recreates it on the right host.
    broken, _ = parse(blueprint("@opaque(/Script/BlueprintGraph.K2Node_CallFunction)"))
    plan = build_plan(plain, broken, "blueprint", dict(), schema)
    assert [verb.op for verb in plan.verbs] == ["delete_node", "create_node"]
    assert plan.verbs[1].args["class"] == "CallFunction"
    assert any(item.code == "node_class_changed" for item in plan.diagnostics)
