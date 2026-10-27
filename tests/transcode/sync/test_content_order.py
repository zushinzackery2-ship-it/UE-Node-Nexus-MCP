"""Issue 21-2b: an instance applies after every source of the parameters it overrides.

An instance checks each override against its parent's parameters, which include
the ones declared inside functions the parent calls. The editor usually holds all
of them already, so availability dropped their edges and the batch fell back to
name order: ``MI_`` sorts before ``M_J``, and the instance applied before the
parameter its parent was about to declare.
"""

import pytest

from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.sync.dependencies import content_sources, order_assets
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.text.parser import parse

from ..fake_parameters import ExposedParameters
from ..fake_ue import FakeUe, SCHEMA_KEY
from ..fixtures import material_function_raw, material_instance_raw, material_raw, prop

PARENT = "/Game/Hair/M_J.M_J"
INSTANCE = "/Game/Hair/MI_Hair.MI_Hair"
FUNCTION = "/Game/Zeta/MF_Display.MF_Display"
SCALAR = "MaterialExpressionScalarParameter"
CALL = "MaterialExpressionMaterialFunctionCall"


def document(asset, cls, body):
    parsed, sink = parse(f"nexus: 1\nasset: {asset.split('.')[0]}\nclass: {cls}\nschema: {SCHEMA_KEY}\n\n{body}\n")
    assert not sink.has_errors, sink.items
    return parsed


def instance(asset=INSTANCE, parent=PARENT):
    return "material_instance", document(asset, "MaterialInstanceConstant", f"[asset]\nParent = {parent}\n\n[scalar]\nV5DisplaySurface = 0.5")


def material(asset=PARENT, calls=(), kind="material"):
    body = "".join(f"call{index} : MaterialFunctionCall(MaterialFunction={function})\n" for index, function in enumerate(calls))
    return kind, document(asset, "MaterialFunction" if kind == "material_function" else "Material", "[graph]\n" + body)


def ordered(documents, held=None):
    sources = content_sources(documents, (held or dict()).get)
    return order_assets(documents, available=set(documents), content=sources)


def test_instance_follows_its_held_parent_despite_name_order():
    documents = dict([(INSTANCE, instance()), (PARENT, material())])
    assert order_assets(documents, available=set(documents)) == [INSTANCE, PARENT]
    assert ordered(documents) == [PARENT, INSTANCE]


def test_instance_follows_a_function_behind_its_unchanged_parent():
    documents = dict([(INSTANCE, instance()), (FUNCTION, material(FUNCTION, kind="material_function"))])
    assert ordered(documents, dict([(PARENT, material(calls=[FUNCTION]))])) == [FUNCTION, INSTANCE]


def test_parent_chains_and_nested_calls_apply_before_their_readers():
    child, parent, base = "/Game/A/MI_Child.MI_Child", "/Game/B/MI_Parent.MI_Parent", "/Game/C/M_Base.M_Base"
    outer, inner = "/Game/D/MF_Outer.MF_Outer", "/Game/E/MF_Inner.MF_Inner"
    documents = dict([(child, instance(child, parent)), (parent, instance(parent, base)), (base, material(base, [outer])),
                      (outer, material(outer, [inner], "material_function")), (inner, material(inner, kind="material_function"))])
    assert ordered(documents) == [inner, outer, base, parent, child]


def test_every_outside_source_is_resolved_once():
    second = "/Game/Hair/MI_Beard.MI_Beard"
    held = dict([(PARENT, material(calls=[FUNCTION])), (FUNCTION, material(FUNCTION, kind="material_function"))])
    calls = []

    def resolve(asset):
        calls.append(asset)
        return held.get(asset)

    sources = content_sources(dict([(INSTANCE, instance()), (second, instance(second))]), resolve)
    assert sources == dict([(INSTANCE, set((PARENT, FUNCTION))), (second, set((PARENT, FUNCTION)))])
    assert sorted(calls) == sorted((PARENT, FUNCTION))


def test_sources_stop_where_the_editor_holds_nothing_and_skip_other_kinds():
    engine = "/Engine/Functions/MF_Engine.MF_Engine"
    blueprint = document("/Game/BP_Door.BP_Door", "Blueprint", f"[asset]\nSkin = {PARENT}")
    documents = dict([(PARENT, material(calls=[engine])), ("/Game/BP_Door.BP_Door", ("blueprint", blueprint))])
    assert content_sources(documents, dict().get) == dict([(PARENT, set((engine,)))])


class ParameterUe(ExposedParameters, FakeUe):
    pass


@pytest.fixture
def legacy(tmp_path):
    parent = material_raw()
    parent["graph"]["nodes"].append(dict(guid="G-CALL", name="Call", class_short="MaterialFunctionCall", x=-600, y=0,
        inputs=["A", "B"], outputs=["Result"], props=[prop("MaterialFunction", "UMaterialFunctionInterface*",
        material_function_raw()["asset_path"], "None")], **dict([("class", "/Script/Engine." + CALL)])))
    child = material_instance_raw()
    child.update(asset_path=INSTANCE, instance=dict(scalar=[dict(name="Gloss", type="float", value="1.000000")]))
    child["props"] = [prop("Parent", "UMaterialInterface", parent["asset_path"], "None")]
    ue = ParameterUe(dict(mf=material_function_raw(), mat=parent, mi=child))
    root = tmp_path / "Content_Transcoded"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    run_sync(ue, "init", env=env)
    publish(root / ".nexus/schema" / SCHEMA_KEY, SCHEMA_KEY, dict(material_expression=dict([
        (SCALAR, dict(path="/Script/Engine." + SCALAR, inputs=[], outputs=[""],
                      props=dict(ParameterName=dict(type="name", default="None"), DefaultValue=dict(type="number", default="0")))),
        (CALL, dict(path="/Script/Engine." + CALL, inputs=["A", "B"], outputs=["Result"], dynamic_pins=True,
                    props=dict(MaterialFunction=dict(type="object", kind="object", default="None"))))])), incremental=True)
    return ue, env, root / "Shadetest"


def edit(file, old, new):
    text = file.read_text(encoding="utf-8")
    assert old in text, text
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


def declare(file):
    edit(file, "[graph]\n", "[graph]\nsurface : ScalarParameter(ParameterName=V5DisplaySurface) @ -600,0\n")


def override(project):
    edit(project / "Hair/MI_Hair.mi.nexus", "[scalar]\n", "[scalar]\nV5DisplaySurface = 0.5\n")


SOURCES = dict(parent=("Materials/M_Glass.mat.nexus", material_raw()["asset_path"]),
               function=("WaterStains/Functions/MF_WS_S.mf.nexus", material_function_raw()["asset_path"]))


@pytest.mark.parametrize("source", sorted(SOURCES))
def test_legacy_push_applies_the_declaring_source_first(legacy, source):
    ue, env, project = legacy
    file, asset = SOURCES[source]
    declare(project / file)
    override(project)

    report = run_sync(ue, "push", [INSTANCE, asset], dict(dry_run=False), env)

    assert report["error_count"] == 0, report["diagnostics"]
    assert [entry["asset_path"] for entry in ue.applied] == [asset, INSTANCE]


def test_legacy_failed_source_blocks_the_instance(legacy):
    ue, env, project = legacy
    file, function = SOURCES["function"]
    declare(project / file)
    override(project)

    def bridge(operation, payload):
        ue.fail_apply_index = 0 if payload.get("asset_path") == function else None
        return ue(operation, payload)

    report = run_sync(bridge, "push", [INSTANCE, function], dict(dry_run=False, stop_on_error=False), env)

    assert [entry["asset_path"] for entry in ue.applied] == [function]
    assert dict((entry["asset"], entry["action"]) for entry in report["rows"])[INSTANCE] == "blocked"
