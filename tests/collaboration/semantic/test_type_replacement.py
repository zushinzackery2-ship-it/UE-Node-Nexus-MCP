"""Same-id replacements retain identity/layout, and replace class metadata."""

from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw, text_of
from ue_node_nexus_mcp.transcode.schema.lock import ClassInfo
from ue_node_nexus_mcp.transcode.text.semantic import value
from tests.transcode.fixtures import material_raw, prop

CONSTANT = "Constant3Vector"
PARAMETER = "VectorParameter"
COLOR = "(R=2,G=2,B=2,A=1)"
DEFAULT = "(R=0,G=0,B=0,A=0)"


def class_info(name):
    name = name.rsplit(".", 1)[-1].removeprefix("MaterialExpression")
    field = "Constant" if name == CONSTANT else "DefaultValue"
    props = dict([(field, dict(type="FLinearColor", default=DEFAULT))])
    if name == PARAMETER:
        props["ParameterName"] = dict(type="FName", default="None")
    return ClassInfo(name=name, path=f"/Script/Engine.MaterialExpression{name}", props=props, outputs=[""])


@pytest.mark.parametrize("before,after,old_field,new_field", [
    (CONSTANT, PARAMETER, "Constant", "DefaultValue"),
    (PARAMETER, CONSTANT, "DefaultValue", "Constant"),
])
def test_same_id_replacement_discards_old_defaults_through_normalization(before, after, old_field, new_field):
    raw = material_raw()
    raw["graph"]["nodes"] = [dict(guid="SAME", name="Main", class_short=before, x=30, y=60,
        props=[prop(old_field, "FLinearColor", COLOR, DEFAULT)], inputs=[], outputs=[""],
        **dict([("class", f"/Script/Engine.MaterialExpression{before}")]))]
    raw["graph"].update(links=[], outputs=[dict(property="BaseColor", from_out=0, **dict([("from", "SAME")]))])
    schema = SimpleNamespace(key="replacement", info=lambda: dict(), resolve_class=lambda family, name, **kwargs: class_info(name))
    original = from_raw(raw, schema=schema)
    text = text_of(original).replace(before + "(", after + "(").replace(old_field + "=", new_field + "=")
    changed = capture(text, original, "author", "material", schema)
    normalized = normalize_snapshot(changed, original, schema)
    identifier, entity = next(iter(normalized["semantic"]["sections"]["graph:"]["entities"].items()))
    assert identifier in original["bindings"]
    assert old_field not in entity["args"]
    assert entity["args"][new_field]["value"] == value(COLOR, "FLinearColor")["value"]
    assert entity["position"] == [30, 60]
    assert normalized["semantic"]["sections"]["graph:"]["links"] == original["semantic"]["sections"]["graph:"]["links"]
    assert old_field not in normalized["bindings"][identifier]["meta"].get("prop_defaults", dict())
