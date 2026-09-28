"""Resolve expression pins from the node's properties, including dynamic pins."""

import re

from ..errors import DiagnosticSink
from ..model import Decl, Section
from ..schema_lock import ClassInfo, SchemaLock
from .interfaces import lookup

_CUSTOM_INPUT_NAME = re.compile(r'InputName\s*=\s*"([^\"]+)"')
_CUSTOM_OUTPUT_NAME = re.compile(r'OutputName\s*=\s*"([^\"]+)"')
_MIP_PINS = dict(TMVM_MipLevel=("MipLevel",), TMVM_MipBias=("MipBias",),
                 TMVM_Derivative=("DDX(UVs)", "DDY(UVs)"))
_MIP_NAMES = frozenset(name for pins in _MIP_PINS.values() for name in pins)
_VIEW_BIAS = "Apply View MipBias"
_TEXTURE_INPUTS = frozenset(("Texture2D", "TextureCube", "Texture2DArray", "TextureExternal", "VolumeTexture"))
_CHANNELS = dict(rgb=0, rgba=0, r=1, g=2, b=3, a=4)
_SINGLE_OUTPUTS = frozenset(("out", "output", "result", "value", "color", "rgb", "rgba", "return"))
MATERIAL_OUTPUT_PROPERTIES = (
    "MaterialAttributes", "BaseColor", "Metallic", "Specular", "Roughness", "Anisotropy", "EmissiveColor", "Opacity",
    "OpacityMask", "Normal", "Tangent", "SubsurfaceColor", "AmbientOcclusion", "Refraction", "CustomizedUVs_0",
    "CustomizedUVs_1", "CustomizedUVs_2", "CustomizedUVs_3", "CustomizedUVs_4", "CustomizedUVs_5", "CustomizedUVs_6",
    "CustomizedUVs_7", "PixelDepthOffset", "ShadingModel", "Displacement",
    "WorldPositionOffset", "ClearCoat", "ClearCoatRoughness", "SurfaceThickness", "FrontMaterial",
)
_ROOT_PROPERTIES = dict((name.replace("_", "").lower(), name) for name in MATERIAL_OUTPUT_PROPERTIES)


def output_property(name):
    return _ROOT_PROPERTIES.get((name or "").replace("_", "").replace(" ", "").lower(), name)


def expression_name(info: ClassInfo) -> str:
    return info.name.rsplit(".", 1)[-1].removeprefix("MaterialExpression")


def input_names(decl: Decl, info: ClassInfo) -> list[str]:
    params = decl.keyed()
    if expression_name(info) == "Custom":
        return _CUSTOM_INPUT_NAME.findall(params["Inputs"]) if "Inputs" in params else info.inputs
    mode_info = info.prop("MipValueMode")
    if mode_info is None or _VIEW_BIAS not in info.inputs:
        return info.inputs
    # UE inserts mode-specific inputs before the view bias, for every subclass.
    mode = params.get("MipValueMode", str(mode_info.get("default", ""))).rsplit("::", 1)[-1]
    base = [name for name in info.inputs if name not in _MIP_NAMES]
    index = base.index(_VIEW_BIAS)
    return [*base[:index], *_MIP_PINS.get(mode, ()), *base[index:]]


def output_names(decl: Decl, info: ClassInfo) -> list[str]:
    if expression_name(info) == "Custom":
        names = _CUSTOM_OUTPUT_NAME.findall(decl.keyed().get("AdditionalOutputs", ""))
        return ["return", *names] if names else [""]
    return info.outputs


def function_pins(decl: Decl, schema: SchemaLock | None) -> tuple[list[str], list[str]] | None:
    path = decl.keyed().get("MaterialFunction")
    record = lookup(path) if path else None
    if record is None:
        record = schema.material_function(path) if path and schema else None
    if record is None:
        return None
    return tuple([str(item.get("name", "")) for item in record.get(field) or []]
                 for field in ("inputs", "outputs"))


def canonical_pin(decl, name, direction, metadata, schema=None):
    """One semantic spelling for omitted pins, reflected names and indices."""
    info = schema.resolve_class("material_expression", decl.type_name) if schema and decl and not decl.opaque else None
    field = "outputs" if direction == "out" else "inputs"
    names = metadata.get(field, [])
    is_function = decl is not None and decl.type_name.rsplit(".", 1)[-1].removeprefix("MaterialExpression") == "MaterialFunctionCall"
    if is_function:
        signature = function_pins(decl, schema)
        if signature is not None:
            names = signature[1 if direction == "out" else 0]
    elif info:
        reflected = output_names(decl, info) if direction == "out" else input_names(decl, info)
        names = reflected or names
    text = "0" if name is None else str(name).strip()
    lowered = text.lower()
    lowered_names = [str(item).lower() for item in names]
    index = int(text) if text.isdigit() else lowered_names.index(lowered) if lowered in lowered_names else None
    if index is None and direction == "in" and len(names) == 1 and lowered in ("a", "in", "input"):
        index = 0
    if index is None and direction == "out":
        if len(names) in (4, 5) and not any(names):
            index = _CHANNELS.get(lowered)
        elif len(names) <= 1 and lowered in _SINGLE_OUTPUTS and (not is_function or names):
            index = 0
    if index is None:
        return text
    if index < len(names) and names[index]:
        return names[index]
    return None if index == 0 else str(index)


def lint_previews(graph: Section, infos: dict[str, ClassInfo | None], sink: DiagnosticSink) -> None:
    connected = frozenset(link.dst for link in graph.links()
                          if link.dst_pin is None or link.dst_pin.lower() in ("preview", "0", "in", "input", "a"))
    for decl in graph.decls():
        info = infos.get(decl.id)
        name = expression_name(info) if info else decl.type_name.rsplit(".", 1)[-1].removeprefix("MaterialExpression")
        if decl.opaque or name != "FunctionInput":
            continue
        metadata = info.prop("InputType") if info else None
        default = str((metadata or dict()).get("default", ""))
        kind = decl.keyed().get("InputType", default).rsplit("::", 1)[-1].removeprefix("FunctionInput_")
        if kind in (_TEXTURE_INPUTS | {"StaticBool"}) and decl.id not in connected:
            sink.error("missing_preview_connection", f"{decl.id} ({kind}) requires a Preview connection", line=decl.line)
