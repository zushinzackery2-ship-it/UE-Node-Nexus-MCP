"""Native Niagara pin spellings and unknown defaults retain their meaning."""

import pytest
from types import SimpleNamespace

from ue_node_nexus_mcp.transcode.raw.niagara import module_decl
from ue_node_nexus_mcp.transcode.text.semantic import normalize
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot
from ue_node_nexus_mcp.transcode.schema.lock import ClassInfo


@pytest.mark.parametrize("type_name,csv,named", [
    ("Vector", "0,0,0", "(X=0,Y=0,Z=0)"),
    ("Vector3f", "1.5,-2,3", "(Z=3,X=1.5,Y=-2)"),
    ("Vector2D", "1.5,-2", "(Y=-2,X=1.5)"),
    ("Quat4f", "0,0,0,1", "(W=1,X=0,Y=0,Z=0)"),
    ("LinearColor", "0.88,0.94,1,1", "(R=0.879999995,G=0.939999998,B=1,A=1)"),
])
def test_native_pin_csv_and_named_components_are_equivalent(type_name, csv, named):
    assert normalize(csv, type_name) == normalize(named, type_name)


def test_component_order_dimension_and_value_remain_significant():
    expected = normalize("(X=1,Y=2,Z=3)", "Vector")
    assert normalize("1,3,2", "Vector") != expected
    assert normalize("1,2", "Vector") != expected
    assert normalize("1,2,4", "Vector") != expected
    assert normalize("1,2,3", "FString") == "1,2,3"


def test_missing_native_default_is_unknown_not_an_empty_value():
    module = dict(script="/Niagara/Module.Module", script_short="Module", inputs=[
        dict(name="Unknown", type="float", has_override=False),
        dict(name="Known", type="float", default="45", has_override=False),
    ])
    assert module_decl("module", module).meta["input_defaults"] == {"Known": "45"}


def test_niagara_color_alias_uses_float32_while_engine_color_stays_bytes():
    header = "nexus: 1\nasset: /Game/Test\nclass: NiagaraSystem\n[user]\n"
    authored = capture(header + "Tint : Color = (R=0.88,G=0.94,B=1,A=1)\n", None, "test", "niagara_system")
    actual = capture(header + "Tint : Color = (R=0.879999995,G=0.939999998,B=1,A=1)\n", authored, "test", "niagara_system")
    assert authored["semantic"] == actual["semantic"]
    changed = capture(header + "Tint : Color = (R=0.9,G=0.94,B=1,A=1)\n", authored, "test", "niagara_system")
    assert authored["semantic"] != changed["semantic"]
    assert normalize("(R=1,G=2,B=3,A=255)", "Color") == "(A=255,B=3,G=2,R=1)"


def test_niagara_module_vector_alias_uses_its_native_float32_contract():
    def state(number):
        return from_raw(dict(raw_version=1, kind="niagara_system", asset_path="/Game/Test.Test",
            class_short="NiagaraSystem", niagara=dict(emitters=[dict(name="Emitter", guid="emitter",
                stacks=[dict(group="ParticleUpdate", modules=[dict(guid="module", script="/Niagara/Module.Module",
                    script_short="Module", inputs=[dict(name="Offset", type="Vector", value=f"(X={number},Y=0,Z=0)",
                    has_override=True)])])])])))["semantic"]
    assert state("0.88") == state("0.879999995")
    assert state("0.88") != state("0.9")


def test_niagara_module_path_and_catalog_alias_share_the_readback_contract():
    path = "/Niagara/Module.Module"
    raw = dict(raw_version=1, kind="niagara_system", asset_path="/Game/Test.Test", class_short="NiagaraSystem",
        niagara=dict(emitters=[dict(name="Emitter", guid="emitter", stacks=[dict(group="ParticleUpdate", modules=[
            dict(guid="module", script=path, script_short="Module", inputs=[
                dict(name="Amount", type="float", value="0.879999995", has_override=True)])])])]))
    actual = from_raw(raw)
    authored = capture("nexus: 1\nasset: /Game/Test\nclass: NiagaraSystem\n"
        "[stack Emitter/ParticleUpdate]\nmodule : /Niagara/Module.Module(Amount=0.88)\n", None, "test", "niagara_system")
    def resolve(family, name, **kwargs):
        return ClassInfo("Module", path) if family == "niagara_modules" and name in (path, "Module") else None
    normalized = normalize_snapshot(authored, actual, SimpleNamespace(key="test", resolve_class=resolve))
    received = next(iter(actual["semantic"]["sections"]["stack:Emitter/ParticleUpdate"]["entities"].values()))
    desired = next(iter(normalized["semantic"]["sections"]["stack:Emitter/ParticleUpdate"]["entities"].values()))
    assert desired["type"] == received["type"]
    assert desired["args"] == received["args"]
