"""Renderer merge IDs survive save-time UObject duplication and legacy history."""

from copy import deepcopy

from tests.transcode.fixtures import niagara_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw, text_of
from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot


MERGE_ID = "57A4EF27-F561-4352-850A-462965654997"


def renderer(snapshot):
    return next(iter(snapshot["semantic"]["sections"]["renderers:Sparks"]["entities"].items()))


def legacy_and_current():
    old = niagara_raw()
    old["ids"] = {"R-1": "authored_flakes"}
    current = deepcopy(old)
    current["niagara"]["emitters"][0]["renderers"][0].update(guid=MERGE_ID, legacy_guid="R-1")
    return from_raw(old), current


def test_legacy_renderer_binding_migrates_without_losing_author_identity():
    old, raw = legacy_and_current()
    current = from_raw(raw, old)
    old_id, old_entity = renderer(old)
    current_id, current_entity = renderer(current)
    assert current_id == old_id
    assert current_entity["alias"] == old_entity["alias"] == "authored_flakes"
    assert current["bindings"][current_id]["physical"] == MERGE_ID
    assert "authored_flakes : Sprite" in text_of(current)
    normalized = normalize_snapshot(old, current)
    assert normalized["bindings"][current_id]["physical"] == MERGE_ID
    assert normalized["semantic"] == current["semantic"]


def test_save_time_object_name_change_preserves_the_merge_identity():
    old, raw = legacy_and_current()
    current = from_raw(raw, old)
    raw["niagara"]["emitters"][0]["renderers"][0]["legacy_guid"] = "RenamedDuringSave_2"
    saved = from_raw(raw, current)
    assert saved["semantic"] == current["semantic"]
    assert renderer(saved)[0] == renderer(current)[0]


def test_replacement_merge_id_does_not_borrow_the_old_identity():
    old, raw = legacy_and_current()
    current = from_raw(raw, old)
    replacement = deepcopy(raw)
    replacement["niagara"]["emitters"][0]["renderers"][0]["guid"] = "A275806F-2199-4DD9-8737-E2EAC2BD6AAC"
    changed = from_raw(replacement, current)
    assert renderer(changed)[0] != renderer(current)[0]


def test_legacy_renderer_names_are_scoped_to_the_emitter():
    old, raw = legacy_and_current()
    second = deepcopy(raw["niagara"]["emitters"][0])
    second.update(name="Other", guid="E-2", stacks=[], opaque_stacks=[])
    second["renderers"][0].update(guid="A275806F-2199-4DD9-8737-E2EAC2BD6AAC")
    raw["niagara"]["emitters"].append(second)
    current = from_raw(raw, old)
    other_id, other = next(iter(current["semantic"]["sections"]["renderers:Other"]["entities"].items()))
    assert other_id != renderer(current)[0]
    assert other["alias"] != renderer(current)[1]["alias"]
