"""Niagara diff: system/emitter props, user params, module stacks, renderers."""

from __future__ import annotations

from .diff_common import decl_params, diff_brace_props, diff_prop_section, same_class
from .model import Decl, Document, Section
from .plan import AssetPlan
from .schema_lock import SchemaLock
from .sync_project import SyncError
from .values import is_marker, values_equal

ASSIGNMENT_MODULE = "SetVariables"


def diff_niagara(local: Document, base: Document | None, plan: AssetPlan, kind: str,
                 schema: SchemaLock | None = None) -> None:
    diff_prop_section(local.section("asset"), base.section("asset") if base else None, plan, "set_asset_prop")
    _diff_user_params(local.section("user"), base.section("user") if base else None, plan)
    if kind == "niagara_system":
        _diff_emitters(local, base, plan)
    for section in local.find_sections("stack"):
        before = base.section("stack", section.args) if base else None
        _diff_stack(section, before, plan, kind, schema)
    for section in base.find_sections("stack") if base else []:
        if local.section("stack", section.args) is None and section.decls() and not section.decls()[0].opaque:
            emitter, group = _split(section.args, kind)
            for decl in section.decls():
                plan.add("ns_module_remove", emitter=emitter, group=group, id=decl.id)
    for section in local.find_sections("renderers"):
        before = base.section("renderers", section.args) if base else None
        _diff_renderers(section, before, plan, schema)
    for section in base.find_sections("renderers") if base else []:
        if local.section("renderers", section.args) is None:
            for decl in section.decls():
                plan.add("ns_renderer_remove", emitter=section.args.strip(), id=decl.id)


def _split(args: str, kind: str) -> tuple[str, str]:
    text = args.strip()
    if kind == "niagara_system" and "/" in text:
        emitter, group = text.split("/", 1)
        return emitter, group
    return "", text


def _diff_user_params(local: Section | None, base: Section | None, plan: AssetPlan) -> None:
    local_decls = local.decl_map() if local else {}
    base_decls = base.decl_map() if base else {}
    for name in base_decls:
        if name not in local_decls:
            plan.add("ns_user_param_remove", name=name)
    for name, decl in local_decls.items():
        before = base_decls.get(name)
        if before is None:
            plan.add("ns_user_param_add", line=decl.line, name=name, type=decl.type_name, value=decl.default or "")
        elif before.type_name != decl.type_name:
            plan.add("ns_user_param_remove", line=decl.line, name=name)
            plan.add("ns_user_param_add", line=decl.line, name=name, type=decl.type_name, value=decl.default or "")
        elif not values_equal(before.default or "", decl.default or ""):
            plan.add("ns_user_param_set", line=decl.line, name=name, value=decl.default or "")


def _diff_emitters(local: Document, base: Document | None, plan: AssetPlan) -> None:
    local_emitters = {section.args.strip(): section for section in local.find_sections("emitter")}
    base_emitters = {section.args.strip(): section for section in (base.find_sections("emitter") if base else [])}
    for name in base_emitters:
        if name not in local_emitters:
            plan.add("ns_emitter_remove", name=name)
    for name, section in local_emitters.items():
        before = base_emitters.get(name)
        props = section.prop_map()
        if before is None:
            plan.add("ns_emitter_add", line=section.line, name=name, parent=props["Parent"].value if "Parent" in props else "")
            for key, prop in props.items():
                if key != "Parent":
                    plan.add("ns_emitter_prop_set", line=prop.line, emitter=name, name=key, value=prop.value)
            continue
        before_props = before.prop_map()
        if (props.get("Parent").value if props.get("Parent") else "") != (before_props.get("Parent").value if before_props.get("Parent") else ""):
            plan.error("unsupported_edit", f"emitter {name}: Parent cannot be changed after creation", section.line)
        for key, prop in props.items():
            if key == "Parent":
                continue
            old = before_props.get(key)
            if old is None or not values_equal(old.value, prop.value):
                plan.add("ns_emitter_prop_set", line=prop.line, emitter=name, name=key, value=prop.value)
        for key, old in before_props.items():
            if key not in props and key != "Parent":
                default = old.default if old.default is not None else ("True" if key == "Enabled" else None)
                if default is None:
                    plan.error("cannot_reset", f"emitter {name}: property {key!r} removed but default unknown", section.line)
                else:
                    plan.add("ns_emitter_prop_set", emitter=name, name=key, value=default)


def _module_script(decl: Decl, schema: SchemaLock | None) -> str | None:
    """Exact script path for a module the text adds; None when the name is ambiguous."""
    recorded = str(decl.meta.get("script") or "")
    if recorded:
        return recorded
    if schema is None or decl.type_name == ASSIGNMENT_MODULE:
        return decl.type_name
    try:
        path, _, _ = schema.niagara_module(decl.type_name)
    except SyncError as exc:
        if exc.code == "schema_ambiguous":
            return None
        raise
    return path or decl.type_name


def _diff_stack(local: Section, base: Section | None, plan: AssetPlan, kind: str, schema: SchemaLock | None = None) -> None:
    emitter, group = _split(local.args, kind)
    if group.startswith("Event:") or group.startswith("Stage:"):
        return
    local_decls = {decl.id: decl for decl in local.decls()}
    base_decls = {decl.id: decl for decl in base.decls()} if base else {}
    common = dict(emitter=emitter, group=group)
    for identifier in base_decls:
        if identifier not in local_decls:
            plan.add("ns_module_remove", id=identifier, **common)
    local_order = [decl.id for decl in local.decls()]
    for index, (identifier, decl) in enumerate(local_decls.items()):
        before = base_decls.get(identifier)
        if before is not None and before.type_name != decl.type_name:
            plan.warn("module_script_changed", f"{identifier}: script changed {before.type_name} -> {decl.type_name}; module is recreated", decl.line)
            plan.add("ns_module_remove", id=identifier, **common)
            before = None
        if before is None:
            if decl.type_name == ASSIGNMENT_MODULE:
                plan.error("unsupported_edit", f"{identifier}: Set Variables modules cannot be created from text; add it in the editor, then pull", decl.line)
                continue
            script = _module_script(decl, schema)
            if script is None:
                plan.error("ambiguous_module", f"{identifier}: module {decl.type_name!r} matches several scripts; write the full path", decl.line)
                continue
            plan.add("ns_module_add", line=decl.line, id=identifier, script=script, index=index, **common)
            for key, value in decl_params(decl).items():
                _add_input(plan, decl, identifier, key, value, None, common)
            if "disabled" in decl.flags:
                plan.add("ns_module_set_enabled", line=decl.line, id=identifier, enabled=False, **common)
            continue
        local_params, base_params = decl_params(decl), decl_params(before)
        for key, value in local_params.items():
            if key not in base_params or not values_equal(base_params[key], value):
                _add_input(plan, decl, identifier, key, value, base_params.get(key), common)
        for key in base_params:
            if key not in local_params:
                default = (before.meta.get("input_defaults") or {}).get(key, "")
                plan.add("ns_module_input_reset", line=decl.line, id=identifier, input=key, default=default, **common)
        if ("disabled" in decl.flags) != ("disabled" in before.flags):
            plan.add("ns_module_set_enabled", line=decl.line, id=identifier, enabled="disabled" not in decl.flags, **common)
    base_order = [identifier for identifier in (decl.id for decl in base.decls()) if identifier in local_decls] if base else []
    surviving = [identifier for identifier in local_order if identifier in base_decls]
    if surviving != base_order:
        for index, identifier in enumerate(local_order):
            plan.add("ns_module_move", id=identifier, index=index, **common)


def _add_input(plan: AssetPlan, decl: Decl, identifier: str, key: str, value: str, before: str | None, common: dict[str, str]) -> None:
    if is_marker(value):
        if before is not None and values_equal(before, value):
            return
        plan.error("unsupported_edit", f"{identifier}.{key}: linked/dynamic inputs (@link/@dynamic) cannot be created from text in this version", decl.line)
        return
    plan.add("ns_module_input_set", line=decl.line, id=identifier, input=key, value=value, **common)


def _diff_renderers(local: Section, base: Section | None, plan: AssetPlan, schema: SchemaLock | None = None) -> None:
    emitter = local.args.strip()
    local_decls = local.decl_map()
    base_decls = base.decl_map() if base else {}
    for identifier in base_decls:
        if identifier not in local_decls:
            plan.add("ns_renderer_remove", emitter=emitter, id=identifier)
    for identifier, decl in local_decls.items():
        before = base_decls.get(identifier)
        if before is not None and not same_class(schema, "niagara_renderer", decl.type_name, before.type_name):
            plan.add("ns_renderer_remove", emitter=emitter, id=identifier)
            before = None
        if before is None:
            plan.add("ns_renderer_add", line=decl.line, emitter=emitter, id=identifier, **{"class": decl.type_name})
            for key, value in decl.prop_values().items():
                plan.add("ns_renderer_set_prop", line=decl.line, emitter=emitter, id=identifier, name=key, value=value)
            continue
        diff_brace_props(decl, before, plan, "ns_renderer_set_prop", emitter=emitter, id=identifier)
