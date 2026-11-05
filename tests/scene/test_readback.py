"""Verify requested scene writes against native sparse properties and serialized transforms."""

from copy import deepcopy
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.readback import scene_result
from .fixtures import MAP


ACTOR = "a123456789abcdef0123456789abcdef"


def case():
    actor = dict(id=ACTOR.upper(), level_path=MAP, components=[],
        **{"class": "/Game/Cameras/BP_Camera.BP_Camera"},
        transform="(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=3.000000,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))",
        properties={}, defaults=dict(WorkSide="512"), property_schema=dict(WorkSide=dict(type="int32")))
    plan = dict(ops=[dict(op="create_actor", id=ACTOR, level_path=MAP,
        **{"class": "/Game/Cameras/BP_Camera"}), dict(op="update_actor", id=ACTOR,
        transform="(Translation=(X=3,Y=0,Z=0))", properties=dict(WorkSide="512"))])
    return actor, dict(id="verified-commit", request=dict(scene_plan=plan))


@pytest.mark.parametrize("mount", ["/Game", "/ForgeRender", "/Tests"])
def test_readback_verifies_supplied_members_and_sparse_native_defaults(mount):
    actor, record = case()
    actor["class"] = actor["class"].replace("/Game", mount)
    operation = record["request"]["scene_plan"]["ops"][0]
    operation["class"] = operation["class"].replace("/Game", mount)
    expected = dict(semantic=dict(unwritten_class_default="old library default"))
    actual = dict(semantic={}, raw=dict(actors=[actor]))
    assert scene_result(SimpleNamespace(), expected, actual, record) == []


def test_native_class_readback_preserves_its_script_object_path():
    actor, record = case()
    actor["class"] = "/Script/Engine.Actor"
    record["request"]["scene_plan"]["ops"][0]["class"] = actor["class"]
    assert scene_result(SimpleNamespace(), {}, dict(raw=dict(actors=[actor])), record) == []
    actor["class"] = "/Script/Engine.Pawn"
    assert scene_result(SimpleNamespace(), {}, dict(raw=dict(actors=[actor])), record)


@pytest.mark.parametrize("defect", ["value", "transform", "class", "missing"])
def test_readback_rejects_wrong_scene_writes_even_when_the_semantic_delta_is_empty(defect):
    actor, record = case()
    if defect == "value":
        actor["defaults"]["WorkSide"] = "256"
    elif defect == "transform":
        actor["transform"] = actor["transform"].replace("3.000000", "3.001000")
    elif defect == "class":
        actor["class"] = "/Game/Cameras/BP_Wrong.BP_Wrong"
    actual = dict(semantic={}, raw=dict(actors=[] if defect == "missing" else [actor]))
    assert scene_result(SimpleNamespace(), dict(semantic={}), actual, record)


def test_transform_export_text_rounding_is_bounded_and_reports_real_deviations():
    actor, record = case()
    wanted = "(Rotation=(X=0,Y=0,Z=-0.382683432,W=0.923879533))"
    record["request"]["scene_plan"]["ops"][1]["transform"] = wanted
    actor["transform"] = "(Rotation=(X=0,Y=0,Z=-0.382683,W=0.923880),Translation=(X=3,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))"
    actual = dict(semantic={}, raw=dict(actors=[actor]))
    assert scene_result(SimpleNamespace(), dict(semantic={}), actual, record) == []
    wrong = deepcopy(actual)
    wrong["raw"]["actors"][0]["transform"] = actor["transform"].replace("0.923880", "0.923890")
    assert scene_result(SimpleNamespace(), dict(semantic={}), wrong, record)


def test_native_property_description_uses_its_nested_array_value_contract():
    actor, record = case()
    contract = dict(type="TArray", kind="array", element=dict(type="TObjectPtr<UMaterialInterface>", kind="object"))
    actor["property_schema"]["Materials"] = dict(type="TArray", kind="array", value_schema=contract)
    actor["properties"]["Materials"] = '("/Script/Engine.MaterialInstanceConstant\'/Game/M.M\'")'
    record["request"]["scene_plan"]["ops"][1]["properties"]["Materials"] = "(/Game/M.M)"
    actual = dict(semantic={}, raw=dict(actors=[actor]))
    assert scene_result(SimpleNamespace(), dict(semantic={}), actual, record) == []
    actor["properties"]["Materials"] = '("/Script/Engine.MaterialInstanceConstant\'/Game/Wrong.Wrong\'")'
    assert scene_result(SimpleNamespace(), dict(semantic={}), actual, record)
