"""Blueprint diff: variables, components, defaults, functions and graphs."""

from __future__ import annotations

from .bp_signature import FunctionSignature, parse_signature
from .bp_types import join_type_text, parse_type_text
from .blueprint.component_order import ordered_components
from .blueprint.members import declarations_first, diff_members, interface_changed
from .blueprint.node_ids import existing_node_ids
from .diff_common import diff_brace_props, diff_prop_section, match_renamed, same_class
from .diff_graph import diff_graph_section
from .lexer import LexError
from .model import Decl, Document, Section
from .plan import AssetPlan
from .schema_lock import SchemaLock
from .values import values_equal


def diff_blueprint(local: Document, base: Document | None, plan: AssetPlan, schema: SchemaLock | None = None) -> None:
    plan.ids = existing_node_ids(base, plan.ids)
    diff_prop_section(local.section("asset"), base.section("asset") if base else None, plan, "set_asset_prop")
    _diff_variables(local.section("variables"), base.section("variables") if base else None, plan)
    _diff_components(local.section("components"), base.section("components") if base else None, plan, schema)
    diff_prop_section(local.section("defaults"), base.section("defaults") if base else None, plan, "bp_default_set")
    diff_members(local, base, plan)
    _diff_graphs(local, base, plan, schema)
    declarations_first(plan)
    plan.interface_changed = interface_changed(plan)


def _variable_payload(decl: Decl) -> dict[str, object]:
    props = decl.prop_values()
    flags = [flag for flag in decl.prop_flags()]
    rep_notify = props.get("RepNotify", "")
    if rep_notify and "Replicated" not in flags:
        flags.append("Replicated")
    return {
        "name": decl.id,
        "type": parse_type_text(join_type_text(decl.type_name, decl.args)),
        "default": decl.default or "",
        "category": props.get("Category", ""),
        "tooltip": props.get("Tooltip", ""),
        "flags": flags,
        "rep_notify": rep_notify,
    }


def _diff_variables(local: Section | None, base: Section | None, plan: AssetPlan) -> None:
    local_decls = local.decl_map() if local else {}
    base_decls = base.decl_map() if base else {}
    renames = match_renamed(local_decls, base_decls)
    renamed_old = set(renames.values())
    for name, old in renames.items():
        plan.add("bp_variable_rename", line=local_decls[name].line, old=old, new=name)
    for name in base_decls:
        if name not in local_decls and name not in renamed_old:
            plan.add("bp_variable_remove", name=name)
    for name, decl in local_decls.items():
        try:
            payload = _variable_payload(decl)
        except LexError as exc:
            plan.error("invalid_type", f"variable {name}: {exc}", decl.line)
            continue
        before = base_decls.get(renames.get(name, name))
        if before is None:
            plan.add("bp_variable_add", line=decl.line, **payload)
            continue
        before_payload = _variable_payload(before)
        changed = {key: value for key, value in payload.items() if key != "name" and not _same(value, before_payload[key])}
        if changed:
            plan.add("bp_variable_set", line=decl.line, name=name, **changed)


def _same(left: object, right: object) -> bool:
    if isinstance(left, str) and isinstance(right, str):
        return values_equal(left, right)
    if isinstance(left, list) and isinstance(right, list):
        return sorted(map(str, left)) == sorted(map(str, right))
    return left == right


def _diff_components(local: Section | None, base: Section | None, plan: AssetPlan, schema: SchemaLock | None = None) -> None:
    local_decls = local.decl_map() if local else {}
    base_decls = base.decl_map() if base else {}
    renames = match_renamed(local_decls, base_decls)
    for name, old in renames.items():
        plan.add("bp_component_rename", line=local_decls[name].line, old=old, new=name)
    for name, before in base_decls.items():
        if name not in local_decls and name not in renames.values():
            if before.inherited:
                plan.error("unsupported_edit", f"inherited component {name!r} cannot be removed from text", None)
            else:
                plan.add("bp_component_remove", name=name)
    for name, decl in ordered_components(local_decls, plan):
        before = base_decls.get(renames.get(name, name))
        keyed = decl.keyed()
        if before is None:
            if decl.inherited:
                plan.error("unsupported_edit", f"{name}: @inherited components come from the parent class and cannot be added here", decl.line)
                continue
            plan.add("bp_component_add", line=decl.line, name=name, **{"class": decl.type_name}, parent=keyed.get("parent", ""), socket=keyed.get("socket", ""))
            for key, value in decl.prop_values().items():
                plan.add("bp_component_set_prop", line=decl.line, name=name, prop=key, value=value)
            continue
        if not decl.inherited and not same_class(schema, "component", decl.type_name, before.type_name):
            plan.error("unsupported_edit", f"{name}: component class change ({before.type_name} -> {decl.type_name}) needs remove + add under a new name", decl.line)
            continue
        before_keyed = before.keyed()
        if keyed.get("parent", "") != before_keyed.get("parent", "") or keyed.get("socket", "") != before_keyed.get("socket", ""):
            plan.add("bp_component_reparent", line=decl.line, name=name, parent=keyed.get("parent", ""), socket=keyed.get("socket", ""))
        diff_brace_props(decl, before, plan, "bp_component_set_prop", key_field="prop", name=name)


def _signature_of(section: Section, plan: AssetPlan) -> FunctionSignature | None:
    try:
        return parse_signature(section.args)
    except LexError as exc:
        plan.error("invalid_signature", f"bad function signature: {exc}", section.line)
        return None


def _diff_graphs(local: Document, base: Document | None, plan: AssetPlan, schema: SchemaLock | None = None) -> None:
    base_graphs = {section.args.strip(): section for section in (base.find_sections("graph") if base else [])}
    for section in local.find_sections("graph"):
        name = section.args.strip()
        before = base_graphs.get(name)
        if before is None and name != "EventGraph":
            plan.add("bp_graph_add", line=section.line, name=name)
        diff_graph_section(section, before, plan, "blueprint", graph=name, schema=schema)
    for name in base_graphs:
        if local.section("graph", name) is None:
            plan.error("unsupported_edit", f"graph {name!r} was removed; deleting ubergraph pages is not supported from text", None)

    base_functions: dict[str, tuple[Section, FunctionSignature]] = {}
    for section in base.find_sections("function") if base else []:
        signature = _signature_of(section, plan)
        if signature is not None:
            base_functions[signature.name] = (section, signature)
    local_names: set[str] = set()
    for section in local.find_sections("function"):
        signature = _signature_of(section, plan)
        if signature is None:
            continue
        local_names.add(signature.name)
        before = base_functions.get(signature.name)
        if before is None:
            if base is None and signature.name == "UserConstructionScript":
                plan.add("bp_function_add", line=section.line, name=signature.name, signature=signature.to_raw(), reuse_builtin=True)
            else:
                plan.add("bp_function_add", line=section.line, name=signature.name, signature=signature.to_raw())
            _diff_locals(section, None, signature.name, plan)
            diff_graph_section(section, None, plan, "blueprint", graph=signature.name, schema=schema)
            continue
        before_section, before_signature = before
        if signature.text() != before_signature.text():
            plan.add("bp_function_signature_set", line=section.line, name=signature.name, signature=signature.to_raw())
        _diff_locals(section, before_section, signature.name, plan)
        diff_graph_section(section, before_section, plan, "blueprint", graph=signature.name, schema=schema)
    for name in base_functions:
        if name not in local_names:
            plan.add("bp_function_remove", name=name)
    for section in local.find_sections("macro"):
        before = base.section("macro", section.args) if base else None
        if before is None or [decl.id for decl in section.decls()] != [decl.id for decl in before.decls()]:
            plan.error("unsupported_edit", f"macro {section.args!r} is read-only in this version", section.line)


def _diff_locals(local: Section, base: Section | None, function: str, plan: AssetPlan) -> None:
    local_locals = {decl.id: decl for decl in local.decls() if decl.modifier == "local"}
    base_locals = {decl.id: decl for decl in base.decls() if decl.modifier == "local"} if base else {}
    for name in base_locals:
        if name not in local_locals:
            plan.add("bp_local_variable_remove", function=function, name=name)
    for name, decl in local_locals.items():
        try:
            type_raw = parse_type_text(join_type_text(decl.type_name, decl.args))
        except LexError as exc:
            plan.error("invalid_type", f"local {name}: {exc}", decl.line)
            continue
        before = base_locals.get(name)
        if before is None:
            plan.add("bp_local_variable_add", line=decl.line, function=function, name=name, type=type_raw, default=decl.default or "")
        elif join_type_text(before.type_name, before.args) != join_type_text(decl.type_name, decl.args) or not values_equal(before.default or "", decl.default or ""):
            plan.add("bp_local_variable_set", line=decl.line, function=function, name=name, type=type_raw, default=decl.default or "")
