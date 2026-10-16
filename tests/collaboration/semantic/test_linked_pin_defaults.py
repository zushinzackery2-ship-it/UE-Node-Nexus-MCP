"""A linked input pin carries no literal default in the canonical state."""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.text.parser import parse

HEADER = "nexus: 1\nasset: /Game/BP_Linked\nclass: Blueprint\nschema: key\n\n"
CONCAT = "CallFunction(KismetStringLibrary.Concat_StrStr"


def _document(body):
    document, sink = parse(HEADER + body)
    assert not sink.has_errors, sink.items
    return document


def _encode(document):
    state, _, _ = encode(document, "blueprint", None, "author", None)
    return dict(semantic=state)


def test_linked_input_default_is_not_encoded():
    document = _document("[graph EventGraph]\nsrc : Self\n"
                         f'b : {CONCAT}, A="pre", B="post")\n'
                         "src.self -> b.A\n")
    snapshot = _encode(document)
    entities = snapshot["semantic"]["sections"]["graph:EventGraph"]["entities"]
    node = next(item for item in entities.values() if item["alias"] == "b")
    assert set(node["args"]) == {"B"}


def test_linked_default_does_not_reach_the_plan():
    authored = _document("[graph EventGraph]\nsrc : Self\n"
                         f'b : {CONCAT}, A="pre", B="post")\n'
                         "src.self -> b.A\n")
    canonical = _document("[graph EventGraph]\nsrc : Self\n"
                          f'b : {CONCAT}, B="post")\n'
                          "src.self -> b.A\n")
    plan = build_plan(to_document(_encode(authored)), canonical, "blueprint", None, None)
    assert plan.verbs == [] and not plan.has_errors


def test_disconnect_precedes_a_new_default():
    connected = _document("[graph EventGraph]\nsrc : Self\n"
                          f'b : {CONCAT}, B="post")\n'
                          "src.self -> b.A\n")
    freed = _document("[graph EventGraph]\nsrc : Self\n"
                      f'b : {CONCAT}, A="new", B="post")\n')
    plan = build_plan(freed, connected, "blueprint", None, None)
    ops = [verb.op for verb in plan.verbs]
    assert "disconnect_pins" in ops and "set_node_param" in ops
    assert ops.index("disconnect_pins") < ops.index("set_node_param")
