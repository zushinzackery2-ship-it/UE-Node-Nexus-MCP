"""Function-call outputs are slots in semantic state and names in checkout text.

Native links select an output by slot, so semantic state stores the slot and an
unchanged default output never turns dirty once the provider interface loads.
The author-facing text keeps the provider's output names.
"""

from unittest.mock import patch

from ue_node_nexus_mcp.transcode.collaboration.semantic.decode import to_document
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, text_of
from ue_node_nexus_mcp.transcode.material.pins import canonical_pin
from ue_node_nexus_mcp.transcode.storage.io import digest
from ue_node_nexus_mcp.transcode.text.model import Decl, Document, Header, Link, Section

PINS = "ue_node_nexus_mcp.transcode.material.pins.function_pins"


def snapshot(outputs):
    graph = Section("graph", entries=[
        Decl("call", "MaterialFunctionCall", args=[("MaterialFunction", "/Game/MF_Provider.MF_Provider")],
             meta=dict(outputs=outputs)),
        Decl("first", "FunctionOutput", args=[("OutputName", "First")]),
        Decl("second", "FunctionOutput", args=[("OutputName", "Second")]),
        Link("call", "Result", "first", None),
        Link("call", "Other", "second", None),
    ])
    document = Document(Header(nexus=1, asset="/Game/MF_Consumer", cls="MaterialFunction"), [graph])
    semantic, bindings, locations = encode(document, "material_function", None, "test")
    return dict(semantic=semantic, semantic_hash=digest(semantic), bindings=bindings, locations=locations)


def test_native_function_output_names_survive_checkout_text():
    original = snapshot(["Result", "Other"])
    document = to_document(original)
    assert [link.src_pin for link in document.section("graph").links()] == ["Result", "Other"]
    reread = capture(text_of(original), original, "test", "material_function")
    assert reread["semantic_hash"] == original["semantic_hash"]


def test_new_call_pins_are_bound_to_the_interface_used_for_encoding():
    with patch(PINS, return_value=([], ["Result", "Other"])):
        original = snapshot([])
    assert [link.src_pin for link in to_document(original).section("graph").links()] == ["Result", "Other"]


def test_unresolvable_slot_is_preserved_for_validation():
    original = snapshot(["Result", "Other"])
    links = next(section for section in original["semantic"]["sections"].values() if section["name"] == "graph")["links"]
    next(iter(links.values()))["src_pin"] = "7"
    assert to_document(original).section("graph").links()[0].src_pin == "7"


def test_first_function_output_matches_native_implicit_output():
    call = Decl(id="call", type_name="MaterialFunctionCall")
    with patch(PINS, return_value=None):
        native = canonical_pin(call, None, "out", {"outputs": [""]})
    with patch(PINS, return_value=(["Input"], ["Result", "Other"])):
        assert canonical_pin(call, None, "out", {}) == native
        assert canonical_pin(call, "Result", "out", {}) == native


def test_nonfirst_function_output_keeps_its_distinct_slot():
    call = Decl(id="call", type_name="MaterialFunctionCall")
    with patch(PINS, return_value=(["Input"], ["Result", "Other"])):
        assert canonical_pin(call, "Other", "out", {}) == "1"
        assert canonical_pin(call, "1", "out", {}) == "1"
        assert canonical_pin(call, "Result", "in", {}) == "Result"
