"""Shared diff helpers: property sections, typed declarations and class identity."""

from __future__ import annotations

from typing import Any

from .bp_call_host import call_spelling
from .model import Decl, Section
from .plan import AssetPlan
from .schema.records import class_aliases
from .schema_lock import SchemaLock
from .sync_project import SyncError
from .values import values_equal


def class_path(schema: SchemaLock | None, family: str, text: str) -> str | None:
    """Record path for one class spelling, or None when it cannot be resolved.

    An ambiguous name (several records share the alias, such as ``CallFunction`` for
    both ``K2Node_CallFunction`` and ``AnimGraphNode_CallFunction``) resolves to None:
    the spelling alone does not identify a class.
    """
    if schema is None or not text:
        return None
    try:
        info = schema.resolve_class(family, text)
    except SyncError:
        return None
    return info.path if info is not None else None


def answers_to(path: str, text: str) -> bool:
    """True when ``text`` is one of the names the class at ``path`` answers to."""
    return bool(path) and bool(text) and text in class_aliases(path, path)


def same_class(schema: SchemaLock | None, family: str, left: str, right: str) -> bool:
    """True when two class spellings denote the same reflected class.

    Text accepts the full UE path, the real class name and the prefix-stripped form
    the mirror writes for nodes, so comparing the spellings literally reads a
    notation switch as a class change and rejects text the schema itself resolved.
    When only one side resolves, that record decides identity, which covers shortcut
    names several records share. Every host class of a function call is the same
    call: the bridge picks the host from the function, not from the spelling.
    """
    if family == "k2node":
        left, right = call_spelling(left), call_spelling(right)
    if left == right:
        return True
    left_path = class_path(schema, family, left)
    right_path = class_path(schema, family, right)
    if left_path is not None and right_path is not None:
        return left_path == right_path
    if left_path is not None:
        return answers_to(left_path, right)
    if right_path is not None:
        return answers_to(right_path, left)
    return False


def prop_default(base_section: Section | None, base_decl_meta: dict[str, Any] | None, key: str) -> str | None:
    """Default value for a property that the local file dropped (== reset)."""
    if base_decl_meta is not None:
        defaults = base_decl_meta.get("prop_defaults") or {}
        if key in defaults:
            return str(defaults[key])
    if base_section is not None:
        prop = base_section.prop_map().get(key)
        if prop is not None and prop.default is not None:
            return prop.default
    return None


def diff_prop_section(local: Section | None, base: Section | None, plan: AssetPlan, op: str, **extra: Any) -> None:
    """Emit ``op(name=, value=)`` for changed/added props and resets for removed ones."""
    local_props = local.prop_map() if local else {}
    base_props = base.prop_map() if base else {}
    for key, prop in local_props.items():
        before = base_props.get(key)
        if before is None or not values_equal(before.value, prop.value):
            plan.add(op, line=prop.line, name=key, value=prop.value, **extra)
    for key, prop in base_props.items():
        if key in local_props:
            continue
        default = prop.default
        if default is None:
            plan.error("cannot_reset", f"property {key!r} was removed but its default value is unknown; write an explicit value", line=local.line if local else None)
            continue
        plan.add(op, name=key, value=default, **extra)


def decl_params(decl: Decl) -> dict[str, str]:
    return {key: value for key, value in decl.args if key is not None}


def diff_params(local: Decl, base: Decl | None, plan: AssetPlan, op: str, skip: set[str] | None = None, **extra: Any) -> None:
    """Diff keyed args of two declarations (node params, module inputs)."""
    skip = skip or set()
    local_params = {key: value for key, value in decl_params(local).items() if key not in skip}
    base_params = {key: value for key, value in decl_params(base).items() if key not in skip} if base else {}
    for key, value in local_params.items():
        if key not in base_params or not values_equal(base_params[key], value):
            plan.add(op, line=local.line, name=key, value=value, **extra)
    for key in base_params:
        if key in local_params:
            continue
        default = prop_default(None, base.meta if base else None, key)
        plan.add(op, line=local.line, name=key, value=default, **extra)


def diff_brace_props(local: Decl, base: Decl | None, plan: AssetPlan, op: str, key_field: str = "name", **extra: Any) -> None:
    """Emit ``op`` for every changed ``{k=v}`` entry; ``key_field`` names the verb arg carrying the key."""
    local_props = local.prop_values()
    base_props = base.prop_values() if base else {}
    for key, value in local_props.items():
        if key not in base_props or not values_equal(base_props[key], value):
            plan.add(op, line=local.line, value=value, **{key_field: key}, **extra)
    for key in base_props:
        if key in local_props:
            continue
        default = prop_default(None, base.meta if base else None, key)
        if default is None:
            plan.error("cannot_reset", f"{local.id}: property {key!r} was removed but its default value is unknown", line=local.line)
            continue
        plan.add(op, line=local.line, value=default, **{key_field: key}, **extra)


def match_renamed(local_decls: dict[str, Decl], base_decls: dict[str, Decl]) -> dict[str, str]:
    """``new_name -> old_name`` for declarations annotated with ``@renamed(Old)``."""
    renames: dict[str, str] = {}
    for name, decl in local_decls.items():
        old = decl.annotations.get("renamed")
        if old and old in base_decls and name not in base_decls:
            renames[name] = old
    return renames
