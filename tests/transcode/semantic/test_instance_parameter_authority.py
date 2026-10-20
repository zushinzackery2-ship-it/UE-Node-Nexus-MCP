"""A parent change must not be compared against the parameter arrays it derives."""

from ue_node_nexus_mcp.transcode.collaboration.semantic.encode import encode
from ue_node_nexus_mcp.transcode.raw.simple import material_instance_document
from ue_node_nexus_mcp.transcode.text.parser import parse

TEXT = """nexus: 1
asset: /Game/NexusProbe/MI_Probe
class: MaterialInstanceConstant
schema: key
[asset]
Parent = /Game/NexusProbe/M_Probe
ScalarParameterValues = ()
[scalar]
Roughness = 0.5
"""


def test_export_drops_reflected_parameter_arrays():
    raw = dict(raw_version=1, kind="material_instance", asset_path="/Game/MI_Probe",
               **{"class": "/Script/Engine.MaterialInstanceConstant"},
               props=[dict(name="Parent", value="/Game/M_Probe"), dict(name="ScalarParameterValues", value="()")],
               instance=dict(scalar=[dict(name="Roughness", value="0.5", type="float")]))
    document = material_instance_document(raw)
    keys = [prop.key for section in document.sections if section.name == "asset" for prop in section.props()]
    assert keys == ["Parent"]


def test_authored_derived_array_is_not_state():
    document, sink = parse(TEXT)
    assert not sink.has_errors
    semantic, _, _ = encode(document, "material_instance", None, "test", None)
    props = semantic["sections"]["asset:"]["props"]
    assert "Parent" in props and "ScalarParameterValues" not in props
    assert semantic["sections"]["scalar:"]["props"]["Roughness"] is not None
