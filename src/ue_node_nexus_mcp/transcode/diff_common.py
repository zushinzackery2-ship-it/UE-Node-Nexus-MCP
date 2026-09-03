"""Shared diff helpers: property sections and typed declarations."""

from __future__ import annotations

from typing import Any

from .model import Decl, Section
from .plan import AssetPlan
from .values import values_equal


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
