"""Issues 2, 3 and 12: what the mirror text has to carry to be re-applicable.

A Blueprint that cannot be recreated from its own mirror, a graph shape the
encoder refuses to represent, and a pin name that changes with the editor's
language are all the same failure: the text is not the asset.
"""

from __future__ import annotations

import copy

import pytest

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode, is_exec_pin
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from tests.transcode.fixtures import blueprint_raw


def document(raw):
    return document_from_raw(raw)[0]


def semantic(raw):
    return encode(document(raw), "blueprint", None, "ue")[0]


def props(text: str) -> dict:
    parsed, _ = parse(text)
    return dict((prop.key, prop.value) for prop in parsed.section("asset").props())


@pytest.mark.parametrize("kind", ["normal", "macro_library", "interface", "function_library", "const"])
def test_the_mirror_records_the_blueprint_type_a_create_would_need(kind):
    raw = copy.deepcopy(blueprint_raw())
    raw["blueprint"]["blueprint_type"] = kind
    text = emit(document(raw))
    assert props(text)["BlueprintType"] == kind
    assert props(text)["ParentClass"] == "/Script/Engine.Actor"


def test_a_mirror_without_a_recorded_type_stays_silent_about_it():
    """An export from an older bridge must not gain an invented BlueprintType."""
    raw = copy.deepcopy(blueprint_raw())
    raw["blueprint"].pop("blueprint_type", None)
    assert "BlueprintType" not in props(emit(document(raw)))


def test_the_recorded_type_survives_a_text_round_trip():
    raw = copy.deepcopy(blueprint_raw())
    raw["blueprint"]["blueprint_type"] = "macro_library"
    text = emit(document(raw))
    reparsed, diagnostics = parse(text)
    assert not diagnostics.has_errors
    assert emit(reparsed) == text


def bindings_for(raw):
    return encode(document(raw), "blueprint", None, "ue")[1]


def test_an_execution_pin_is_recognized_from_either_end():
    raw = copy.deepcopy(blueprint_raw())
    bindings = bindings_for(raw)
    event = next(key for key, value in bindings.items() if value["meta"].get("class_short") == "Event")
    call = next(key for key, value in bindings.items() if value["meta"].get("class_short") == "CallFunction")

    assert is_exec_pin(bindings, event, "then", "out") is True
    assert is_exec_pin(bindings, call, "execute", "in") is True
    assert is_exec_pin(bindings, call, "InString", "in") is False
    # An endpoint the bindings know nothing about cannot claim to be execution.
    assert is_exec_pin(bindings, "@missing", "then", "out") is False


def test_two_execution_lines_may_join_at_one_input(monkeypatch):
    """Issue 3: rejoining after a branch is the most ordinary graph there is."""
    raw = copy.deepcopy(blueprint_raw())
    graph = raw["blueprint"]["graphs"][0]
    second = copy.deepcopy(graph["nodes"][0])
    second.update(guid="N-1b", title="Event ActorBeginOverlap",
                  config=dict(function_owner="/Script/Engine.Actor", function_name="ReceiveActorBeginOverlap"))
    second["pins"] = [dict(pin, guid=pin["guid"] + "b", linked=["P-3"] if pin["name"] == "then" else [])
                      for pin in second["pins"]]
    graph["nodes"].insert(1, second)
    target = next(node for node in graph["nodes"] if node["guid"] == "N-3")
    target["pins"][0]["linked"] = ["P-1", "P-1b"]

    links = semantic(raw)["sections"]["graph:EventGraph"]["links"]
    sources = sorted(link["src_pin"] for link in links.values() if link["dst_pin"] == "execute")
    assert sources == ["then", "then"], links


def test_one_execution_output_still_drives_one_place():
    raw = copy.deepcopy(blueprint_raw())
    graph = raw["blueprint"]["graphs"][0]
    event = next(node for node in graph["nodes"] if node["guid"] == "N-1")
    event["pins"][0]["linked"] = ["P-3", "P-8"]
    timeline = next(node for node in graph["nodes"] if node["guid"] == "N-4")
    timeline["pins"][0]["linked"] = ["P-1"]

    with pytest.raises(SyncError) as failure:
        semantic(raw)
    assert failure.value.code == "pin_cardinality"


def test_a_data_input_still_takes_one_source():
    raw = copy.deepcopy(blueprint_raw())
    graph = raw["blueprint"]["graphs"][0]
    getter = next(node for node in graph["nodes"] if node["guid"] == "N-2")
    twin = copy.deepcopy(getter)
    twin.update(guid="N-2b")
    twin["pins"] = [dict(pin, guid=pin["guid"] + "b") for pin in twin["pins"]]
    twin["pins"][0]["linked"] = ["P-5"]
    graph["nodes"].insert(2, twin)
    call = next(node for node in graph["nodes"] if node["guid"] == "N-3")
    duration = next(pin for pin in call["pins"] if pin["name"] == "Duration")
    duration["linked"] = ["P-2", "P-2b"]

    with pytest.raises(SyncError) as failure:
        semantic(raw)
    assert failure.value.code == "pin_cardinality"


def with_cast(raw):
    """A cast whose result pin is wired, so the pin name reaches the link text."""
    graph = raw["blueprint"]["graphs"][0]
    graph["nodes"].append({
        "guid": "N-5", "class": "/Script/BlueprintGraph.K2Node_DynamicCast", "class_short": "DynamicCast",
        "x": 1200, "y": 0, "title": "Cast To Pawn", "enabled": True, "comment": "",
        "config": {"target_type": "/Script/Engine.Pawn"}, "props": [], "supported": True,
        "pins": [
            {"guid": "P-10", "name": "execute", "dir": "in", "type": {"category": "exec"}, "default": "", "hidden": False, "linked": ["P-9"]},
            {"guid": "P-11", "name": "Object", "dir": "in", "type": {"category": "object", "subobject": "/Script/CoreUObject.Object"}, "default": "", "hidden": False, "linked": []},
            {"guid": "P-12", "name": "AsResult", "dir": "out", "type": {"category": "object", "subobject": "/Script/Engine.Pawn"}, "default": "", "hidden": False, "linked": ["P-4s"]},
            {"guid": "P-13", "name": "bSuccess", "dir": "out", "type": {"category": "bool"}, "default": "", "hidden": False, "linked": []},
        ],
    })
    timeline = next(node for node in graph["nodes"] if node["guid"] == "N-4")
    next(pin for pin in timeline["pins"] if pin["name"] == "Update")["linked"] = ["P-10"]
    call = next(node for node in graph["nodes"] if node["guid"] == "N-3")
    next(pin for pin in call["pins"] if pin["name"] == "InString")["linked"] = ["P-12"]
    return raw


def test_a_cast_result_pin_is_written_under_its_language_independent_alias():
    """Issue 12: the bridge exports AsResult, so the text is portable as written."""
    text = emit(document(with_cast(copy.deepcopy(blueprint_raw()))))
    assert "cast_pawn.AsResult -> print_string.InString" in text
    # Nothing the mirror writes depends on the editor's display language.
    assert text.isascii()


def test_a_cast_result_link_survives_a_text_round_trip():
    raw = with_cast(copy.deepcopy(blueprint_raw()))
    text = emit(document(raw))
    reparsed, diagnostics = parse(text)
    assert not diagnostics.has_errors
    assert emit(reparsed) == text
    links = semantic(raw)["sections"]["graph:EventGraph"]["links"]
    assert any(link["src_pin"] == "AsResult" for link in links.values())
