"""Regress the native static-bool reset rejected by SurfaceResolve07."""

import pytest

from ue_node_nexus_mcp.transcode.text.semantic import equivalent, field_values, value, values_equal


@pytest.mark.parametrize("type_name", ("bool", "NiagaraBool", "FNiagaraBool", "struct(Niagara.NiagaraBool)"))
@pytest.mark.parametrize("actual", ("False", "false", "0"))
def test_native_reset_matches_the_declared_false_default(type_name, actual):
    name = "Manually Enable Rotational Solver"
    expected = value("false", type_name, "default")
    observed = field_values({name: actual}, {name: type_name}, {name: "false"})[name]
    assert observed["state"] == "default"
    assert equivalent(expected, observed)


@pytest.mark.parametrize("type_name", ("NiagaraBool", "FNiagaraBool"))
def test_real_boolean_changes_and_lost_history_remain_distinct(type_name):
    assert values_equal("True", "true", type_name)
    assert not values_equal("True", "false", type_name)
    assert not values_equal("()", "false", type_name)


def test_textual_false_is_not_a_boolean_property():
    assert not values_equal("False", "false", "FString")
