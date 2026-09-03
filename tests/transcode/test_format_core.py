from __future__ import annotations

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.lexer import parse_kv_list, split_top_level
from ue_node_nexus_mcp.transcode.model import Decl, Link, Prop
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.values import format_value, normalize_value, parse_value, values_equal

SAMPLE = """nexus: 1
asset: /Game/WaterStains/Functions/MF_WS_S
class: MaterialFunction
schema: 5.5.4-a1b2c3

[asset]
Description = "smoothstep, denom = (B-A)+1e-6"
bExposeToLibrary = True

[graph]
in_a    : FunctionInput(InputName=A, InputType=FunctionInput_Scalar, SortPriority=0) @ -1400,0
sub_ba  : Subtract(Desc="denom = B - A") @ -1000,80
c_eps   : Constant(R=0.000001) @ -1000,220
add_eps : Add @ -800,120
old     : @opaque(/Script/Engine.MaterialExpressionCustom) @ -600,0
out     : FunctionOutput(OutputName=Result) @ -200,40

in_a -> sub_ba.B
sub_ba -> add_eps.A
c_eps -> add_eps.B
add_eps -> out

[variables]
Health : float = 100 { Category=Stats, InstanceEditable, ExposeOnSpawn }
Hits   : int = 0 @renamed(HitCount)
"Spawn Rate" : float = 100

[function TakeDamage(Amount: float) -> (Dead: bool) {Category=Combat, Public}]
local Remaining : float = 0
cmp : CallFunction(KismetMathLibrary.LessEqual_FloatFloat, B=0) @ 0,0 !disabled
entry.Amount -> cmp.A
cmp -> result.Dead
"""


def test_split_top_level_respects_quotes_and_groups() -> None:
    assert split_top_level('a=1, b=(1,2), c="x, y"', ",") == ["a=1", " b=(1,2)", ' c="x, y"']
    assert parse_kv_list('A, k=v, s="a=b"') == [(None, "A"), ("k", "v"), ("s", '"a=b"')]


def test_values_round_trip_and_normalize() -> None:
    assert parse_value(format_value("denom = B - A")) == "denom = B - A"
    assert format_value("(R=1.000000,G=0.500000,B=0.000000,A=1.000000)") == "(R=1,G=0.5,B=0,A=1)"
    assert format_value("/Game/T/T_Rock.T_Rock") == "/Game/T/T_Rock.T_Rock"
    assert format_value("") == '""'
    assert format_value("a\"b") == '"a\\"b"'
    assert normalize_value("True") == "true"
    assert values_equal("1.000000", "1")
    assert values_equal('(X=0.000,Y=50.000)', "(X=0,Y=50)")
    assert not values_equal("1.5", "1.50001")


def test_parse_sections_and_entries() -> None:
    document, sink = parse(SAMPLE, file="MF_WS_S.mf.nexus")
    assert sink.items == []
    assert document.header.asset == "/Game/WaterStains/Functions/MF_WS_S"
    assert document.header.cls == "MaterialFunction"
    graph = document.section("graph")
    assert graph is not None
    decls = graph.decl_map()
    assert decls["in_a"].keyed() == {"InputName": "A", "InputType": "FunctionInput_Scalar", "SortPriority": "0"}
    assert decls["sub_ba"].keyed()["Desc"] == "denom = B - A"
    assert decls["sub_ba"].pos == (-1000, 80)
    assert decls["old"].opaque and decls["old"].marker_arg() == "/Script/Engine.MaterialExpressionCustom"
    links = graph.links()
    assert links[0] == Link("in_a", None, "sub_ba", "B", line=links[0].line)
    assert links[-1] == Link("add_eps", None, "out", None, line=links[-1].line)

    variables = document.section("variables")
    assert variables is not None
    health = variables.decl_map()["Health"]
    assert health.type_name == "float" and health.default == "100"
    assert health.prop_flags() == ["InstanceEditable", "ExposeOnSpawn"]
    assert health.prop_values() == {"Category": "Stats"}
    assert variables.decl_map()["Hits"].annotations == {"renamed": "HitCount"}
    assert "Spawn Rate" in variables.decl_map()

    function = document.section("function")
    assert function is not None
    assert function.args == "TakeDamage(Amount: float) -> (Dead: bool) {Category=Combat, Public}"
    local = function.decls()[0]
    assert local.modifier == "local" and local.id == "Remaining"
    assert function.decls()[1].flags == ["disabled"]
    assert function.decls()[1].positional() == ["KismetMathLibrary.LessEqual_FloatFloat"]


def test_emit_parse_is_idempotent() -> None:
    document, sink = parse(SAMPLE)
    assert sink.items == []
    text = emit(document)
    document2, sink2 = parse(text)
    assert sink2.items == []
    assert emit(document2) == text


def test_prop_line_with_spaces_in_key() -> None:
    document, sink = parse("nexus: 1\nasset: /Game/A\nclass: X\nschema: k\n[scalar]\n玻璃缩放 = 500\nSpawn Rate = 1.500000\n")
    assert sink.items == []
    props = document.section("scalar").prop_map()
    assert props["玻璃缩放"].value == "500"
    assert props["Spawn Rate"].value == "1.500000"
    assert "Spawn Rate = 1.5" in emit(document)


def test_syntax_errors_carry_line_numbers() -> None:
    bad = "nexus: 1\nasset: /Game/A\nclass: X\nschema: k\n[graph]\nn : Add(\n"
    _, sink = parse(bad, file="bad.mat.nexus")
    assert sink.has_errors
    assert sink.errors()[0].line == 6
    assert sink.errors()[0].file == "bad.mat.nexus"


def test_emit_entry_shapes() -> None:
    assert emit(parse("nexus: 1\nasset: a\nclass: b\nschema: c\n[asset]\nK = \"a b\"\n")[0]).endswith('[asset]\nK = "a b"\n')
    decl = Decl(id="n", type_name="Add", args=[("Desc", "x y")], pos=(1, 2))
    text = emit_decl_text(decl)
    assert text == 'n : Add(Desc="x y") @ 1,2'
    assert Prop("k", "v").value == "v"


def emit_decl_text(decl: Decl) -> str:
    from ue_node_nexus_mcp.transcode.emitter import emit_decl

    return emit_decl(decl)
