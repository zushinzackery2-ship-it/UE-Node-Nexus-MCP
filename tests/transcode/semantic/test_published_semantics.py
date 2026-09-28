"""Issues2: publication must preserve values and establish a canonical ancestor."""

from copy import deepcopy
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.planning import merge
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.merge.engine import merge_snapshots
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw, text_of
from ue_node_nexus_mcp.transcode.text.semantic import field_values, normalize, value
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace.service import checkout
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.transcode.fixtures import material_raw

from tests.support.semantics import instance_text, instance_raw, receipt_workspace, self_graph_raw


def test_author_and_native_float_values_share_the_actual_float32_value():
    assert normalize("0.338", "float") == normalize("0.337999999", "float")
    assert normalize("1e-12", "float") != "0"
    assert normalize("1.2345678901234567", "double") != normalize("1.234567890123456", "double")
    assert normalize('"0.338"', "FString") == "0.338"


def test_material_instance_sections_supply_types_before_the_first_export():
    authored = capture(instance_text(), None, "author", "material_instance")
    exported = from_raw(instance_raw(), authored)
    assert authored["semantic"]["sections"]["scalar:"]["props"] == exported["semantic"]["sections"]["scalar:"]["props"]
    edited = capture(instance_text("0.5"), exported, "author", "material_instance")
    assert not merge_snapshots(authored, edited, exported).conflicts


def test_explicit_cdo_default_is_canonicalized_and_does_not_conflict():
    explicit = field_values(dict(R="0.000"), dict(R="float"), dict(R="0"))
    omitted = field_values(dict(), dict(R="float"), dict(R="0"))
    assert explicit == omitted
    base = from_raw(material_raw())
    entity = next(iter(base["semantic"]["sections"]["graph:"]["entities"]))
    base["semantic"]["sections"]["graph:"]["entities"][entity]["args"]["R"] = value("0", "float", "default")
    ours, theirs = deepcopy(base), deepcopy(base)
    ours["semantic"]["sections"]["graph:"]["entities"][entity]["args"]["R"] = value("0", "float")
    theirs["semantic"]["sections"]["graph:"]["entities"][entity]["args"]["R"] = value("1", "float")
    result = merge_snapshots(base, ours, theirs)
    assert not result.conflicts
    assert result.candidate["semantic"]["sections"]["graph:"]["entities"][entity]["args"]["R"]["value"] == "1"


def test_a_published_ancestor_replaces_legacy_author_type_metadata(tmp_path):
    store, asset = Store(tmp_path), "/Game/MI_Test.MI_Test"
    history = History(store)
    root = history.create(history.tree(dict()), [], "empty")
    author = capture(instance_text(), None, "author", "material_instance")
    author["semantic"]["sections"]["scalar:"]["props"]["Amount"] = value("0.338", "text")
    author_id = store.objects.put("snapshot", author)
    submitted = history.create(history.tree(dict([(asset, author_id)])), [root], "new MI")
    canonical = from_raw(instance_raw(), author)
    canonical_id = store.objects.put("snapshot", canonical)
    published = history.create(history.tree(dict([(asset, canonical_id)])), [submitted], "publish")
    workspace = checkout(store, published)
    key = workspace.state["id"] + ":" + asset
    store.put_record("integration", key, dict(workspace_id=workspace.state["id"], asset=asset, source_commit=submitted,
                     source_snapshot=author_id, published_commit=published), [submitted, published], expected=0)
    current = capture(instance_text("0.5"), canonical, "author", "material_instance")
    edited = history.create(history.tree(dict([(asset, store.objects.put("snapshot", current))])), [published], "edit MI")
    inputs = merge(workspace, edited, published, [asset])
    base_id = history.entries(inputs["base"])[asset]
    assert store.objects.data(base_id, "snapshot")["semantic"] == canonical["semantic"]


def test_delete_receipt_cannot_resurrect_the_deleted_asset(tmp_path):
    candidate = from_raw(material_raw())
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    record = dict(id="apply", candidate=commit, asset=asset, request=dict(delete_asset=True), receipt=dict(after=material_raw()))
    with pytest.raises(SyncError) as error:
        actual_snapshot(workspace, record)
    assert error.value.code == "receipt_invalid"


def test_readback_reports_applied_value_differences(tmp_path):
    raw = material_raw()
    candidate = from_raw(raw)
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    node = next(item for item in raw["graph"]["nodes"] if item["class_short"] == "Constant")
    field = next(item for item in node["props"] if item["name"] == "R")
    expected = field["value"]
    alias = next(binding["alias"] for binding in candidate["bindings"].values() if binding.get("physical") == node["guid"])
    field["value"] = "999"
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="set_node_param", id=alias, name=field["name"], value=expected, line=7)]),
                  receipt=dict(after=raw, response_data=dict()))
    with pytest.raises(SyncError) as error:
        actual_snapshot(workspace, record)
    assert error.value.code == "apply_result_mismatch"
    assert error.value.details["diagnostics"]


def test_omitted_self_pin_and_named_self_pin_are_one_connection():
    base = from_raw(self_graph_raw())
    text = text_of(base)
    omitted = text.replace(".self", "")
    authored = capture(omitted, base, "author", "blueprint")
    assert authored["semantic"]["sections"]["graph:EventGraph"]["links"] == base["semantic"]["sections"]["graph:EventGraph"]["links"]


def test_partial_publication_normalizes_the_consumed_author_baseline(tmp_path):
    store, asset = Store(tmp_path), "/Game/MI_Test.MI_Test"
    history = History(store)
    root = history.create(history.tree(dict()), [], "empty")
    author = capture(instance_text(), None, "author", "material_instance")
    author["semantic"]["sections"]["scalar:"]["props"]["Amount"] = value("0.338", "text")
    author_id = store.objects.put("snapshot", author)
    submitted = history.create(history.tree(dict([(asset, author_id)])), [root], "submit")
    canonical = from_raw(instance_raw(), author)
    canonical_id = store.objects.put("snapshot", canonical)
    published = history.create(history.tree(dict([(asset, canonical_id)])), [root], "partial publish")
    workspace = checkout(store, submitted)
    key = workspace.state["id"] + ":" + asset
    store.put_record("integration", key, dict(workspace_id=workspace.state["id"], asset=asset, source_commit=submitted,
                     source_snapshot=author_id, published_commit=published), [submitted, published], expected=0)
    edited_state = capture(instance_text("0.5"), author, "author", "material_instance")
    edited = history.create(history.tree(dict([(asset, store.objects.put("snapshot", edited_state))])), [submitted], "keep editing")
    inputs = merge(workspace, edited, published, [asset])
    states = [store.objects.data(history.entries(inputs[role])[asset], "snapshot") for role in ("base", "ours", "theirs")]
    result = merge_snapshots(*states)
    assert not result.conflicts
    assert result.candidate["semantic"]["sections"]["scalar:"]["props"]["Amount"] == value("0.5", "float")


def test_readback_rejects_an_unexpected_remaining_connection(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.apply.readback import verify_result

    actual = from_raw(self_graph_raw())
    expected = deepcopy(actual)
    expected["semantic"]["sections"]["graph:EventGraph"]["links"] = dict()
    workspace = SimpleNamespace(schema=None, state=dict(files=dict()), root=tmp_path)
    record = dict(id="disconnect", asset="/Game/BP_Test.BP_Test",
                  request=dict(plan=[dict(op="disconnect_pins", graph="EventGraph")]))
    with pytest.raises(SyncError) as error:
        verify_result(workspace, record, expected, actual)
    assert error.value.code == "apply_result_mismatch"
    assert error.value.details["diagnostics"][0]["field"][:2] == ["graph:EventGraph", "links"]
