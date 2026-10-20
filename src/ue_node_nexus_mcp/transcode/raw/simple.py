"""MaterialInstance / generic property-bag / stub raw exports -> Document.

Raw ``instance`` shape (only overridden parameters are present)::

    {"scalar": [{"name","value"}], "vector": [...], "texture": [...],
     "switch": [...], "component_mask": [...]}

Raw stub shape::

    {"tags": [{"name","value"}]}
"""

from __future__ import annotations

from typing import Any

from ..text.model import Document, Prop, Section
from .common import header_from_raw, props_section, require_kind

INSTANCE_SECTIONS = ("scalar", "vector", "texture", "switch", "component_mask", "runtime_virtual_texture", "sparse_volume_texture", "font")
# The structured sections above are the authority for instance parameters; these
# reflected arrays are rebuilt from them and must never be compared as authored state.
DERIVED_PARAMETER_ARRAYS = frozenset((
    "ScalarParameterValues", "VectorParameterValues", "TextureParameterValues", "FontParameterValues",
    "RuntimeVirtualTextureParameterValues", "SparseVolumeTextureParameterValues", "StaticSwitchParameterValues",
    "StaticComponentMaskParameterValues",
))
INSTANCE_TYPES = dict(scalar="float", vector="FLinearColor", texture="UTexture", switch="bool",
                      component_mask="FStaticComponentMask", runtime_virtual_texture="URuntimeVirtualTexture",
                      sparse_volume_texture="USparseVolumeTexture", font="FFontParameterValue")


def material_instance_document(raw: dict[str, Any], schema=None) -> Document:
    require_kind(raw, "material_instance")
    document = Document(header=header_from_raw(raw, schema))
    props = [item for item in raw.get("props") or [] if item.get("name") not in DERIVED_PARAMETER_ARRAYS]
    document.sections.append(props_section(props))
    instance = raw.get("instance") or {}
    for name in INSTANCE_SECTIONS:
        items = instance.get(name) or []
        if not items:
            continue
        section = Section(name=name)
        for item in items:
            section.entries.append(Prop(key=str(item.get("name", "")), value=str(item.get("value", "")), type_name=item.get("type")))
        document.sections.append(section)
    return document


def generic_asset_document(raw: dict[str, Any], schema=None) -> Document:
    require_kind(raw, "asset")
    document = Document(header=header_from_raw(raw, schema))
    document.sections.append(props_section(raw.get("props")))
    return document


def stub_document(raw: dict[str, Any], schema=None) -> Document:
    require_kind(raw, "stub")
    document = Document(header=header_from_raw(raw, schema))
    section = Section(name="tags")
    for item in raw.get("tags") or []:
        section.entries.append(Prop(key=str(item.get("name", "")), value=str(item.get("value", ""))))
    document.sections.append(section)
    return document
