"""Issue 21-2b: a publication applies an instance after every source of its parameters.

The reported batch published two parent materials and their hair instances; the
parents gained a function call declaring ``V5DisplaySurface``. The isolated worker
applied an instance first, by name order, and UE refused its override because the
parent did not declare the parameter yet. Sources stay ordering constraints even
when the editor already holds them, past an unchanged parent included.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.units import changed_closure
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fake_parameters import ExposedParameters
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_function_raw, material_instance_raw, prop

PARENT = "/Game/Materials/M_Glass.M_Glass"
FUNCTION = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
INSTANCE = "/Game/Hair/MI_Hair.MI_Hair"
SCALAR = "MaterialExpressionScalarParameter"
CALL = "MaterialExpressionMaterialFunctionCall"


class ParameterUe(ExposedParameters, ProtocolUe):
    pass


@pytest.fixture
def project(tmp_path):
    ue = ParameterUe()
    ue.assets[FUNCTION] = material_function_raw()
    ue.assets[PARENT]["graph"]["nodes"].append(dict(guid="G-CALL", name="Call", class_short="MaterialFunctionCall", x=-600, y=0,
        inputs=["A", "B"], outputs=["Result"], props=[prop("MaterialFunction", "UMaterialFunctionInterface*", FUNCTION, "None")],
        **dict([("class", "/Script/Engine." + CALL)])))
    child = material_instance_raw()
    child.update(asset_path=INSTANCE, instance=dict(scalar=[dict(name="Gloss", type="float", value="1.000000")]))
    child["props"] = [prop("Parent", "UMaterialInterface", PARENT, "None")]
    ue.assets[INSTANCE] = child
    root = tmp_path / "decoded"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="hair"), env=env)
    publish(root / ".nexus/schema" / SCHEMA_KEY, SCHEMA_KEY, dict(material_expression=dict([
        (SCALAR, dict(path="/Script/Engine." + SCALAR, inputs=[], outputs=[""],
                      props=dict(ParameterName=dict(type="name", default="None"), DefaultValue=dict(type="number", default="0")))),
        (CALL, dict(path="/Script/Engine." + CALL, inputs=["A", "B"], outputs=["Result"], dynamic_pins=True,
                    props=dict(MaterialFunction=dict(type="object", kind="object", default="None"))))])), incremental=True)
    return ue, env, workspace


def edit(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, text
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def publish_override(project, source, **options):
    ue, env, workspace = project
    edit(workspace, source, "[graph]\n", "[graph]\nsurface : ScalarParameter(ParameterName=V5DisplaySurface) @ -600,0\n")
    edit(workspace, INSTANCE, "[scalar]\n", "[scalar]\nV5DisplaySurface = 0.5\n")
    settings = dict(workspace_id=workspace["id"], dry_run=False)
    run_sync(ue, "commit", options=dict(settings, all=True, message="declare and override V5DisplaySurface"), env=env)
    return run_sync(ue, "push", [INSTANCE, source], dict(settings, **options), env=env)


@pytest.mark.parametrize("source", [PARENT, FUNCTION])
def test_the_declaring_source_applies_before_the_instance(project, source):
    ue = project[0]
    assert sorted((INSTANCE, source))[0] == INSTANCE

    result = publish_override(project, source)

    assert result["status"] == "published", result
    assert [payload["asset_path"] for payload in ue.applied] == [source, INSTANCE]
    assert "V5DisplaySurface" in ue.exposed(INSTANCE)


def test_a_failed_source_blocks_the_instance(project):
    ue = project[0]
    ue.fail_save = set((FUNCTION,))

    result = publish_override(project, FUNCTION, stop_on_error=False)

    assert result["status"] in ("blocked", "partial"), result
    assert [payload["asset_path"] for payload in ue.applied] == [FUNCTION]
    assert result["errors"][INSTANCE]["code"] == "dependency_failed", result["errors"]
    assert result["errors"][INSTANCE]["assets"] == [FUNCTION]


def test_isolation_closure_reaches_a_source_behind_an_unchanged_parent():
    planned = dict([(INSTANCE, dict(dependencies=[PARENT], sources=[PARENT, FUNCTION])), (FUNCTION, dict(dependencies=[]))])
    assert changed_closure(planned, dict(), INSTANCE) == set((INSTANCE, FUNCTION))
    assert changed_closure(planned, dict([(FUNCTION, dict(code="failed"))]), INSTANCE) is None
