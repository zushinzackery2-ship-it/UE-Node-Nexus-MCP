from copy import deepcopy

import pytest

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.scene.codec import from_raw
from ue_node_nexus_mcp.transcode.scene.diff import build_plan
from ue_node_nexus_mcp.transcode.scene.model import from_document
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fixtures import ACTOR, COMPONENT, INSTANCE_A, INSTANCE_B, actor, snapshot, transform


def document_and_base(raw: dict):
    document, aliases = from_raw(raw)
    parsed, sink = parse(emit(document))
    assert not sink.has_errors
    return parsed, dict(raw, aliases=aliases)


def test_roundtrip_keeps_actor_component_and_instance_identity():
    raw = snapshot()
    document, base = document_and_base(raw)
    desired, aliases = from_document(document, base)
    owner = desired["actors"][0]
    assert owner["id"] == ACTOR
    assert owner["components"][0]["id"] == COMPONENT
    assert [row["id"] for row in owner["components"][0]["instance_data"]["instances"]] == [INSTANCE_A, INSTANCE_B]
    assert aliases == base["aliases"]
    assert build_plan(raw, desired, dict(), False)["ops"] == []


def test_instance_removal_requires_delete_permission_and_preserves_remaining_ids():
    raw = snapshot()
    document, base = document_and_base(raw)
    section = next(section for section in document.sections if section.name == "instances")
    section.entries.pop(0)
    desired, _ = from_document(document, base)
    with pytest.raises(SyncError, match="allow_delete"):
        build_plan(raw, desired, dict(), False)
    ops = build_plan(raw, desired, dict(), True)["ops"]
    update = next(op for op in ops if op["op"] == "instances_set")
    assert update["removed_count"] == 1
    assert update["instances"][0]["id"] == INSTANCE_B
    assert update["instances"][0]["custom_data"] == [3.0, 4.0]


def test_physical_instance_order_does_not_recreate_instances():
    raw = snapshot()
    document, base = document_and_base(raw)
    desired, _ = from_document(document, base)
    desired["actors"][0]["components"][0]["instance_data"]["instances"].reverse()
    assert build_plan(raw, desired, dict(), False)["ops"] == []


@pytest.mark.parametrize("value", ["(nan,1)", "(1)", "(1e100,2)"])
def test_custom_data_rejects_non_finite_or_wrong_channel_count(value):
    document, base = document_and_base(snapshot())
    instance = next(section for section in document.sections if section.name == "instances").decls()[0]
    instance.props = [(key, value if key == "CustomData" else current) for key, current in instance.props]
    with pytest.raises(SyncError, match="finite floats"):
        from_document(document, base)


def test_duplicate_ids_are_rejected_before_planning():
    document, base = document_and_base(snapshot())
    document.section("actors").entries.append(deepcopy(document.section("actors").decls()[0]))
    with pytest.raises(SyncError) as error:
        from_document(document, base)
    assert error.value.code == "duplicate_scene_identifier"


def test_construction_owned_instances_are_read_only():
    raw = snapshot()
    raw["actors"][0]["components"][0]["instance_data"]["read_only"] = True
    document, base = document_and_base(raw)
    desired, _ = from_document(document, base)
    desired["actors"][0]["components"][0]["instance_data"]["instances"][0]["transform"] = transform(99)
    with pytest.raises(SyncError) as error:
        build_plan(raw, desired, dict(), True)
    assert error.value.code == "construction_instances_read_only"


def test_parent_cycle_is_rejected():
    raw = snapshot([actor(), actor("a" * 32, "Child", [])])
    document, base = document_and_base(raw)
    left, right = document.section("actors").decls()
    left.props.append(("Parent", right.id))
    right.props.append(("Parent", left.id))
    with pytest.raises(SyncError) as error:
        from_document(document, base)
    assert error.value.code == "scene_attachment_cycle"


def test_reflected_property_named_label_keeps_its_own_namespace():
    raw = snapshot()
    owner = raw["actors"][0]
    owner["properties"]["Label"] = "CustomValue"
    owner["property_schema"]["Label"] = dict(type="string")
    owner["defaults"]["Label"] = ""
    document, base = document_and_base(raw)
    assert "Property.Label" in emit(document)
    desired, _ = from_document(document, base)
    assert desired["actors"][0]["label"] == "Owner"
    assert desired["actors"][0]["properties"]["Label"] == "CustomValue"
