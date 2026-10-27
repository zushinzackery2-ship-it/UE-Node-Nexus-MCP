"""Reflected array defaults preserve each native element and strict overrides."""

import pytest

from ue_node_nexus_mcp.transcode.text.semantic import normalize


def postprocess():
    color = dict(kind="struct", type="FLinearColor", fields=dict(
        R=dict(type="float", default="0"), G=dict(type="float", default="0"),
        B=dict(type="float", default="0"), A=dict(type="float", default="1")))
    fields = dict((f"LensFlareTints[{index}]", dict(color, default=f"(R={index / 8},G=0,B=0,A=1)",
                  array_index=index, array_dim=8)) for index in range(8))
    fields["WeightedBlendables"] = dict(kind="struct", type="FWeightedBlendables", default="()",
        fields=dict(Array=dict(kind="array", type="TArray<FWeightedBlendable>", default="", element=dict(type="text"))))
    return dict(kind="struct", type="FPostProcessSettings", fields=fields)


def test_all_eight_distinct_defaults_expand_and_single_override_is_strict():
    contract = postprocess()
    expanded = "(" + ",".join(f"LensFlareTints[{i}]=(R={i / 8},G=0,B=0,A=1)" for i in range(8)) + ",WeightedBlendables=(Array=()))"
    assert normalize("()", contract) == normalize(expanded, contract)
    override = "(LensFlareTints[7]=(R=0.5))"
    assert normalize(override, contract) != normalize("()", contract)
    assert normalize(override, contract) == normalize(expanded.replace("R=0.875", "R=0.5"), contract)


def test_empty_array_native_text_requires_array_contract():
    array = dict(kind="array", type="TArray<FString>", element=dict(type="FString"))
    assert normalize("", array) == normalize("()", array)
    assert normalize("", array) != normalize('("")', array)
    assert normalize("", dict(type="FString")) != normalize("()", dict(type="FString"))


def test_nested_empty_array_and_nonempty_array_differ():
    nested = postprocess()["fields"]["WeightedBlendables"]
    assert normalize("()", nested) == normalize("(Array=())", nested)
    assert normalize("()", nested) != normalize("(Array=((Weight=1)))", nested)


@pytest.mark.parametrize("index", range(8))
def test_each_fixed_element_override_stays_observable(index):
    contract = postprocess()
    assert normalize("()", contract) != normalize(f"(LensFlareTints[{index}]=(R=0.12345))", contract)
