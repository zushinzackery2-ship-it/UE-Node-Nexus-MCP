from __future__ import annotations

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.transcode.collaboration.merge.engine import merge_snapshots
from ue_node_nexus_mcp.transcode.collaboration.merge.order import merge_order
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw, text_of
from ue_node_nexus_mcp.transcode.collaboration.semantic.values import normalize, value
from ue_node_nexus_mcp.transcode.values import normalize_value
from tests.transcode.fixtures import blueprint_raw, material_function_raw, material_instance_raw, material_raw, niagara_raw, prop


@pytest.mark.parametrize("factory", [material_raw, material_function_raw, material_instance_raw, blueprint_raw, niagara_raw])
def test_raw_text_semantic_round_trip(factory):
    base = from_raw(factory())
    captured = capture(text_of(base), base, "workspace-A", base["semantic"]["kind"])
    assert captured["semantic"] == base["semantic"]


def _graph():
    base = from_raw(material_raw())
    graph = next(item for item in base["semantic"]["sections"].values() if item["name"] == "graph")
    return base, graph


def test_distinct_fields_merge_symmetrically_without_replaying_nodes():
    base, graph = _graph()
    ours, theirs = deepcopy(base), deepcopy(base)
    key = next(iter(graph["entities"]))
    ours["semantic"]["sections"]["graph:"]["entities"][key]["annotations"]["comment"] = "ours"
    theirs["semantic"]["sections"]["graph:"]["entities"][key]["position"] = [500, 400]
    result = merge_snapshots(base, ours, theirs)
    swapped = merge_snapshots(base, theirs, ours)
    assert not result.conflicts
    assert result.candidate["semantic"] == swapped.candidate["semantic"]
    assert len(result.candidate["semantic"]["sections"]["graph:"]["entities"]) == len(graph["entities"])


def test_competing_sources_conflict_on_destination_slot():
    base, graph = _graph()
    ours, theirs = deepcopy(base), deepcopy(base)
    slot = next(iter(graph["links"]))
    ours["semantic"]["sections"]["graph:"]["links"][slot]["src_pin"] = "R"
    theirs["semantic"]["sections"]["graph:"]["links"][slot]["src_pin"] = "B"
    result = merge_snapshots(base, ours, theirs)
    conflict = next(item for item in result.conflicts if item["conflict_type"] == "value-conflict")
    assert conflict["field_path"] == ["sections", "graph:", "links", slot]
    assert conflict["ours"]["src_pin"] == "R"
    assert conflict["theirs"]["src_pin"] == "B"


def test_delete_node_and_modify_its_link_is_a_structural_conflict():
    base, graph = _graph()
    ours, theirs = deepcopy(base), deepcopy(base)
    slot, link = next(iter(graph["links"].items()))
    removed = link["src"]
    ours_graph = ours["semantic"]["sections"]["graph:"]
    del ours_graph["entities"][removed]
    ours_graph["links"] = dict((key, edge) for key, edge in ours_graph["links"].items() if edge["src"] != removed and edge["dst"] != removed)
    theirs["semantic"]["sections"]["graph:"]["links"][slot]["src_pin"] = "R"
    result = merge_snapshots(base, ours, theirs)
    assert any(item["conflict_type"] == "modify-delete" for item in result.conflicts)


def test_type_replacement_and_parameter_edit_conflict():
    base, graph = _graph()
    ours, theirs = deepcopy(base), deepcopy(base)
    key = next(iter(graph["entities"]))
    ours["semantic"]["sections"]["graph:"]["entities"][key]["type"] = "Multiply"
    theirs["semantic"]["sections"]["graph:"]["entities"][key]["args"]["R"] = value("5", "float")
    assert any(item["conflict_type"] == "type-conflict" for item in merge_snapshots(base, ours, theirs).conflicts)


def test_default_override_missing_and_text_are_distinct():
    assert value("01", "String") != value("1", "String")
    assert value("01.00", "float") == value("1", "float")
    assert value("true", "bool") == value("1", "bool")
    assert value("0", "float", "default") != value("0", "float")
    assert value(None) != value("")


def test_stack_relative_order_and_concurrent_insertions():
    assert merge_order(["a", "b", "c", "d"], ["b", "a", "c", "d"], ["a", "b", "d", "c"])[0] == ["b", "a", "d", "c"]
    assert merge_order(["a", "b"], ["a", "x", "b"], ["a", "y", "b"])[1]
    assert merge_order(["a", "b"], ["a", "x", "b"], ["a", "b"])[0] == ["a", "x", "b"]


def test_new_physical_guid_does_not_reuse_deleted_entity_identity():
    raw = material_raw()
    base = from_raw(raw)
    raw["graph"]["nodes"][0]["guid"] = "A-DIFFERENT-GUID"
    changed = from_raw(raw, base)
    before = set(base["bindings"])
    assert set(changed["bindings"]) - before


def second_emitter(emitter, name: str, guid: str, suffix: str):
    """Another emitter whose renderer keeps the per-emitter object name it shares."""
    clone = deepcopy(emitter)
    clone["name"] = name
    clone["guid"] = guid
    for stack in clone.get("stacks") or []:
        for module in stack.get("modules") or []:
            module["guid"] = str(module.get("guid", "")) + suffix
    return clone


def renderer_bindings(snapshot) -> dict[str, dict]:
    return dict((identifier, binding) for identifier, binding in snapshot["bindings"].items() if str(binding.get("scope", "")).startswith("renderers:"))


def test_a_custom_struct_value_survives_the_text_round_trip():
    """An unknown struct type must reach the semantic state in the mirror's own number form.

    A capture keeps the value it read from the editor; the file projection renders the
    semantic state, so a type the semantic layer did not recognize left the workspace
    looking locally modified the moment it was created.
    """
    struct = "(ShadingModel=MSM_DefaultLit,DisplacementScaling=(Magnitude=4.000000,Center=0.500000))"
    assert normalize(struct, "BasePropertyOverrides") == normalize_value(struct)
    assert normalize(struct, "BasePropertyOverrides") == "(ShadingModel=MSM_DefaultLit,DisplacementScaling=(Magnitude=4,Center=0.5))"
    assert normalize(normalize(struct, "BasePropertyOverrides"), "BasePropertyOverrides") == normalize(struct, "BasePropertyOverrides")

    raw = material_raw()
    raw["props"].append(prop("BasePropertyOverrides", "FBasePropertyOverrides", struct, "(ShadingModel=MSM_DefaultLit)"))
    base = from_raw(raw)
    assert capture(text_of(base), base, "workspace-A", base["semantic"]["kind"])["semantic"] == base["semantic"]


def test_renderer_identity_is_scoped_to_its_emitter():
    """A renderer id is an object name, so two emitters would otherwise share one entity."""
    raw = niagara_raw()
    emitter = raw["niagara"]["emitters"][0]
    raw["niagara"]["emitters"].append(second_emitter(emitter, "Sparks2", "E-2", "-B"))

    base = from_raw(raw)
    sprites = renderer_bindings(base)
    assert len(sprites) == 2
    assert len({binding["physical"] for binding in sprites.values()}) == 1
    assert len({binding["meta"]["owner"] for binding in sprites.values()}) == 2
    assert len(set(sprites)) == 2

    again = from_raw(raw, base)
    assert set(again["bindings"]) == set(base["bindings"])
    assert set(renderer_bindings(again)) == set(sprites)
