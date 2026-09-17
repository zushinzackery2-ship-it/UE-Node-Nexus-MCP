"""Niagara system / emitter raw export -> Document.

Raw ``niagara`` shape::

    {"user_params": [{"name","type","value"}],
     "emitters": [{"name","guid","enabled":true,"parent":"", "props":[...],
                   "stacks":[{"group":"ParticleUpdate","modules":[{"guid","script","script_short",
                              "enabled":true,"inputs":[{"name","type","value","default",
                              "has_override":true,"linked":null,"dynamic":false}]}]}],
                   "opaque_stacks":[{"group":"Event:Collision","t3d":"..."}],
                   "renderers":[{"guid","class","class_short","props":[...]}]}]}

System documents prefix stack/renderer sections with the emitter name
(``[stack Sparks/ParticleUpdate]``); emitter assets omit the prefix.
"""

from __future__ import annotations

from typing import Any

from .ids import niagara_module_candidate, short_class_name
from .model import Decl, Document, Prop, Section
from .raw_common import allocator_for, claim_section_ids, header_from_raw, non_default_params, prop_defaults, prop_types, prop_values, props_section, require_kind, section_allocator

STACK_GROUPS = ("EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate")


def input_value_text(item: dict[str, Any]) -> str:
    linked = item.get("linked")
    if linked:
        return f"@link({linked})"
    if item.get("dynamic"):
        return "@dynamic"
    return str(item.get("value", ""))


def module_decl(identifier: str, module: dict[str, Any], schema=None) -> Decl:
    args: list[tuple[str | None, str]] = []
    inputs = list(module.get("inputs") or [])
    for item in inputs:
        if not item.get("has_override"):
            continue
        args.append((str(item.get("name", "")), input_value_text(item)))
    script = str(module.get("script", ""))
    short = str(module.get("script_short") or niagara_module_candidate(script))
    decl = Decl(
        id=identifier,
        type_name=module_reference(schema, script, short),
        args=args,
        meta={
            "guid": str(module.get("guid", "")),
            "script": script,
            "inputs": inputs,
            "input_types": {str(item.get("name", "")): str(item.get("type", "")) for item in inputs},
            "input_defaults": {str(item.get("name", "")): str(item.get("default", "")) for item in inputs},
        },
    )
    if module.get("enabled") is False or str(module.get("enabled", "true")).lower() == "false":
        decl.flags.append("disabled")
    return decl


def module_reference(schema, script: str, short: str) -> str:
    """Write the short name only while it still means exactly this script."""
    if schema is None or not script:
        return short
    from .schema.catalog import module_reference as resolve

    return resolve(schema, script, short)


def renderer_type_name(renderer: dict[str, Any]) -> str:
    """``/Script/Niagara.NiagaraSpriteRendererProperties`` -> ``Sprite``."""
    return short_class_name(str(renderer.get("class") or renderer.get("class_short") or ""))


def renderer_decl(identifier: str, renderer: dict[str, Any], owner: str = "") -> Decl:
    props = [(key, value) for key, value in non_default_params(renderer.get("props"))]
    return Decl(
        id=identifier,
        type_name=renderer_type_name(renderer),
        props=props,
        meta={
            "guid": str(renderer.get("guid", "")),
            # Renderer ids are object names, unique per emitter only; semantic identity
            # scopes them by the owning emitter so multi-emitter systems stay distinct.
            "owner": owner,
            "class": str(renderer.get("class", "")),
            "prop_types": prop_types(renderer.get("props")),
            "prop_defaults": prop_defaults(renderer.get("props")),
            "prop_values": prop_values(renderer.get("props")),
        },
    )


def _section_guids(emitter: dict[str, Any]) -> list[list[str]]:
    """Guids of every section ``_emitter_sections`` allocates, in allocation order."""
    sections = [[str(module.get("guid", "")) for module in stack.get("modules") or []] for stack in emitter.get("stacks") or []]
    renderers = [str(item.get("guid", "")) for item in emitter.get("renderers") or []]
    if renderers:
        sections.append(renderers)
    return sections


def _emitter_sections(emitter: dict[str, Any], prefix: str, claimed: dict[str, str], used: set[str], schema=None) -> tuple[list[Section], dict[str, str], dict[str, list[str]]]:
    sections: list[Section] = []
    ids: dict[str, str] = {}
    order: dict[str, list[str]] = {}
    owner = str(emitter.get("guid", ""))
    for stack in emitter.get("stacks") or []:
        group = str(stack.get("group", ""))
        modules = list(stack.get("modules") or [])
        args = f"{prefix}/{group}" if prefix else group
        key = f"stack:{args}"
        allocator = section_allocator([str(module.get("guid", "")) for module in modules], claimed, used)
        section = Section(name="stack", args=args)
        module_ids: list[str] = []
        for module in modules:
            guid = str(module.get("guid", ""))
            identifier = allocator.allocate(guid, niagara_module_candidate(str(module.get("script", ""))), "module")
            module_ids.append(identifier)
            section.entries.append(module_decl(identifier, module, schema))
        ids.update(allocator.by_guid)
        used.update(allocator.by_guid.values())
        order[key] = module_ids
        if modules or group in STACK_GROUPS:
            sections.append(section)
    for opaque in emitter.get("opaque_stacks") or []:
        group = str(opaque.get("group", ""))
        section = Section(name="stack", args=f"{prefix}/{group}" if prefix else group)
        section.entries.append(Decl(id="opaque", type_name="@opaque", args=[(None, group)], meta={"t3d": opaque.get("t3d", "")}))
        sections.append(section)
    renderers = list(emitter.get("renderers") or [])
    if renderers:
        allocator = section_allocator([str(item.get("guid", "")) for item in renderers], claimed, used)
        section = Section(name="renderers", args=prefix)
        for renderer in renderers:
            identifier = allocator.allocate(str(renderer.get("guid", "")), renderer_type_name(renderer), "renderer")
            section.entries.append(renderer_decl(identifier, renderer, owner))
        ids.update(allocator.by_guid)
        used.update(allocator.by_guid.values())
        sections.append(section)
    return sections, ids, order


def niagara_document(
    raw: dict[str, Any],
    previous_ids: dict[str, str] | None = None,
    previous_order: dict[str, list[str]] | None = None,
    schema=None,
) -> tuple[Document, dict[str, str], dict[str, list[str]]]:
    kind = require_kind(raw, "niagara_system", "niagara_emitter")
    niagara = raw.get("niagara") or {}
    document = Document(header=header_from_raw(raw))
    document.sections.append(props_section(raw.get("props")))

    user = Section(name="user")
    for param in niagara.get("user_params") or []:
        user.entries.append(Decl(id=str(param.get("name", "")), type_name=str(param.get("type", "")), default=str(param.get("value", "")), meta={"type": param.get("type")}))
    if user.entries:
        document.sections.append(user)

    emitters = list(niagara.get("emitters") or [])
    known_ids = dict(allocator_for(raw, previous_ids).by_guid)
    claimed, used = claim_section_ids([guids for emitter in emitters for guids in _section_guids(emitter)], known_ids)
    ids: dict[str, str] = {}
    order: dict[str, list[str]] = {}
    for emitter in emitters:
        name = str(emitter.get("name", ""))
        prefix = name if kind == "niagara_system" else ""
        if kind == "niagara_system":
            section = Section(name="emitter", args=name)
            section.entries.append(Prop(key="Enabled", value="True" if emitter.get("enabled", True) else "False", type_name="bool"))
            parent = str(emitter.get("parent", "") or "")
            if parent:
                section.entries.append(Prop(key="Parent", value=parent, type_name="UNiagaraEmitter"))
            section.entries.extend(props_section(emitter.get("props")).entries)
            document.sections.append(section)
        else:
            document.sections[0].entries.extend(props_section(emitter.get("props")).entries)
        sections, emitter_ids, emitter_order = _emitter_sections(emitter, prefix, claimed, used, schema)
        ids.update(emitter_ids)
        order.update(emitter_order)
        document.sections.extend(sections)
    return document, ids, order
