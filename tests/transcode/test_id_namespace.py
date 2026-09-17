"""Id bookkeeping: one asset keeps a single id namespace across all its sections.

The bridge resolves a text id to a GUID through one flat map per asset, so two nodes
in different graphs (Blueprint) or stacks (Niagara) must never answer to one id.
"""

from __future__ import annotations

import collections
import copy
from typing import Any

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.raw_common import claim_section_ids, section_allocator

from .fixtures import blueprint_raw, material_raw, niagara_raw

# FunctionEntry / FunctionResult are named per function graph on purpose: the bridge
# resolves them inside the target graph, so their names repeat on every function page.
SECTION_LOCAL = {"entry", "result"}


def duplicate_ids(ids: dict[str, str]) -> set[str]:
    """Ids handed to more than one guid, section-local names aside."""
    counter = collections.Counter(ids.values())
    return {identifier for identifier, count in counter.items() if count > 1} - SECTION_LOCAL


def regraph(graph: dict[str, Any], name: str, suffix: str) -> dict[str, Any]:
    """Copy of a Blueprint graph whose nodes and pins get fresh guids."""
    clone = copy.deepcopy(graph)
    clone["name"] = name
    pins: dict[str, str] = {}
    for node in clone.get("nodes") or []:
        guid = str(node.get("guid", ""))
        node["guid"] = guid + suffix
        for pin in node.get("pins") or []:
            old = str(pin.get("guid", ""))
            pin["guid"] = old + suffix
            pins[old] = pin["guid"]
    for node in clone.get("nodes") or []:
        for pin in node.get("pins") or []:
            pin["linked"] = [pins[value] for value in pin.get("linked") or [] if value in pins]
    return clone


def reemitter(emitter: dict[str, Any], name: str, suffix: str) -> dict[str, Any]:
    """Copy of a Niagara emitter whose modules and renderers get fresh guids."""
    clone = copy.deepcopy(emitter)
    clone["name"] = name
    clone["guid"] = str(emitter.get("guid", "")) + suffix
    for stack in clone.get("stacks") or []:
        for module in stack.get("modules") or []:
            module["guid"] = str(module.get("guid", "")) + suffix
    for renderer in clone.get("renderers") or []:
        renderer["guid"] = str(renderer.get("guid", "")) + suffix
    return clone


def test_first_claimer_keeps_a_duplicated_id_and_the_loser_is_suffixed() -> None:
    claimed, used = claim_section_ids([["g1", "g2"], ["g3"]], {"g1": "call", "g2": "call", "g3": "call"})

    assert claimed == {"g1": "call"}
    assert used == {"call"}
    assert section_allocator(["g3"], claimed, used).allocate("g3", "call") == "call_2"


def test_blueprint_ids_are_unique_across_graphs_and_survive_a_repull() -> None:
    raw = blueprint_raw()
    graphs = raw["blueprint"]["graphs"]
    clone = copy.deepcopy(graphs[0]["nodes"][2])  # the EventGraph PrintString call
    clone["guid"] = "F-9"
    graphs[1]["nodes"].append(clone)

    document, ids, order = document_from_raw(raw)

    assert ids["N-3"] == "print_string"
    assert ids["F-9"] == "print_string_2"
    assert duplicate_ids(ids) == set()
    text = emit(document)
    calls = [line.split(":", 1)[0].strip() for line in text.splitlines() if "CallFunction(KismetSystemLibrary.PrintString)" in line]
    assert calls == ["print_string", "print_string_2"]

    again, ids_again, order_again = document_from_raw(raw, ids, order)
    assert ids_again == ids and order_again == order
    assert emit(again) == text


def test_every_function_graph_keeps_its_own_entry_and_result() -> None:
    raw = blueprint_raw()
    graphs = raw["blueprint"]["graphs"]
    graphs.append(regraph(graphs[1], "TakeDamage2", "-2"))

    document, ids, _ = document_from_raw(raw)

    assert (ids["F-1"], ids["F-3"]) == ("entry", "result")
    assert (ids["F-1-2"], ids["F-3-2"]) == ("entry", "result")
    assert duplicate_ids(ids) == set()
    text = emit(document)
    assert text.count("entry.then -> result.execute") == 2
    parsed, sink = parse(text)
    assert sink.items == []
    assert lint_document(parsed, "blueprint", None, "BP_Door.bp.nexus").errors() == []


def test_niagara_ids_are_unique_across_emitters() -> None:
    raw = niagara_raw()
    emitters = raw["niagara"]["emitters"]
    emitters.append(reemitter(emitters[0], "Sparks2", "-B"))

    document, ids, order = document_from_raw(raw)

    assert duplicate_ids(ids) == set()
    assert ids["M-1"] == "spawn_rate" and ids["M-1-B"] == "spawn_rate_2"
    assert ids["M-2-B"] == "initialize_particle_2"
    assert ids["M-3-B"] == "gravity_force_2"
    assert ids["R-1-B"] == "sprite_2"
    text = emit(document)
    assert "[stack Sparks2/ParticleUpdate]" in text
    assert "gravity_force_2 : GravityForce(Gravity=(X=0,Y=0,Z=-980)) !disabled" in text

    again, ids_again, order_again = document_from_raw(raw, ids, order)
    assert ids_again == ids and order_again == order
    assert emit(again) == text


def test_material_bookkeeping_reports_only_live_nodes() -> None:
    raw = material_raw()
    live = str(raw["graph"]["nodes"][2]["guid"])
    previous = {live: "denom_b_a", "G-REMOVED": "denom_b_a"}

    _, ids, _ = document_from_raw(raw, previous, None)

    assert "G-REMOVED" not in ids
    assert ids[live] == "denom_b_a"
    assert duplicate_ids(ids) == set()
