"""MaterialInstance / generic property-bag / stub raw exports -> Document.

Raw ``instance`` shape (only overridden parameters are present)::

    {"scalar": [{"name","value"}], "vector": [...], "texture": [...],
     "switch": [...], "component_mask": [...]}

Raw stub shape::

    {"tags": [{"name","value"}]}
"""

from __future__ import annotations

from typing import Any

from .model import Document, Prop, Section
from .raw_common import header_from_raw, props_section, require_kind

INSTANCE_SECTIONS = ("scalar", "vector", "texture", "switch", "component_mask", "runtime_virtual_texture", "sparse_volume_texture", "font")


def material_instance_document(raw: dict[str, Any]) -> Document:
    require_kind(raw, "material_instance")
    document = Document(header=header_from_raw(raw))
    document.sections.append(props_section(raw.get("props")))
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


def generic_asset_document(raw: dict[str, Any]) -> Document:
    require_kind(raw, "asset")
    document = Document(header=header_from_raw(raw))
    document.sections.append(props_section(raw.get("props")))
    return document


def stub_document(raw: dict[str, Any]) -> Document:
    require_kind(raw, "stub")
    document = Document(header=header_from_raw(raw))
    section = Section(name="tags")
    for item in raw.get("tags") or []:
        section.entries.append(Prop(key=str(item.get("name", "")), value=str(item.get("value", ""))))
    document.sections.append(section)
    return document
