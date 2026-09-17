"""Shared raw-export (UE JSON) -> Document helpers.

Raw envelope written by the UE plugin::

    {"raw_version": 1, "asset_path": "/Game/A/M_X.M_X", "class": "/Script/Engine.Material",
     "class_short": "Material", "kind": "material", "schema_key": "...",
     "saved_hash": "...", "dirty": false,
     "props": [{"name", "type", "value", "default"}], ...kind specific...}

Python appends ``ids`` (guid -> id) and ``order`` (section -> id list) when it
stores the file as the sync base.
"""

from __future__ import annotations

from typing import Any

from .ids import IdAllocator
from .model import Decl, Document, Header, Link, Prop, Section
from .paths import package_name
from .values import values_equal

RAW_VERSION = 1


class RawError(ValueError):
    pass


def header_from_raw(raw: dict[str, Any]) -> Header:
    return Header(
        nexus=1,
        asset=package_name(str(raw.get("asset_path", ""))),
        cls=str(raw.get("class_short") or short_class(str(raw.get("class", "")))),
        schema=str(raw.get("schema_key", "")),
    )


def short_class(class_path: str) -> str:
    return class_path.rsplit(".", 1)[-1]


def prop_entries(props: list[dict[str, Any]] | None, *, include_defaults: bool = False) -> list[Prop]:
    entries: list[Prop] = []
    for item in props or []:
        name = str(item.get("name", ""))
        value = str(item.get("value", ""))
        default = item.get("default")
        default_text = None if default is None else str(default)
        if not name:
            continue
        if not include_defaults and default_text is not None and values_equal(value, default_text):
            continue
        entries.append(Prop(key=name, value=value, type_name=item.get("type"), default=default_text))
    return entries


def props_section(raw_props: list[dict[str, Any]] | None, name: str = "asset", args: str = "") -> Section:
    section = Section(name=name, args=args)
    section.entries.extend(prop_entries(raw_props))
    return section


def non_default_params(props: list[dict[str, Any]] | None) -> list[tuple[str, str]]:
    """Node params rendered inline: non-default editable properties, raw order."""
    params: list[tuple[str, str]] = []
    for prop in prop_entries(props):
        params.append((prop.key, prop.value))
    return params


def prop_types(props: list[dict[str, Any]] | None) -> dict[str, str]:
    return {str(item.get("name")): str(item.get("type", "")) for item in props or [] if item.get("name")}


def prop_defaults(props: list[dict[str, Any]] | None) -> dict[str, str]:
    return {str(item.get("name")): str(item.get("default", "")) for item in props or [] if item.get("name") and item.get("default") is not None}


def prop_values(props: list[dict[str, Any]] | None) -> dict[str, str]:
    return {str(item.get("name")): str(item.get("value", "")) for item in props or [] if item.get("name")}


def order_ids(candidates: list[str], depends_on: dict[str, set[str]], sort_key: dict[str, tuple[Any, ...]], previous: list[str] | None) -> list[str]:
    """Keep the previous order for known ids; place new ids topologically.

    New ids are inserted after the last of their dependencies that already sits
    in the ordered list, breaking ties by ``sort_key`` (usually x, y, id).
    """
    candidate_set = set(candidates)
    known = [identifier for identifier in (previous or []) if identifier in candidate_set]
    known_set = set(known)
    remaining = sorted(
        (identifier for identifier in candidates if identifier not in known_set),
        key=lambda item: sort_key.get(item, (0, 0, item)),
    )
    ordered = list(known)
    placed = set(ordered)
    while remaining:
        ready = [identifier for identifier in remaining if depends_on.get(identifier, set()) & candidate_set <= placed]
        if not ready:
            ordered.extend(remaining)
            break
        for identifier in ready:
            ordered.append(identifier)
            placed.add(identifier)
            remaining.remove(identifier)
    return ordered


def base_order(raw: dict[str, Any], section_key: str) -> list[str] | None:
    order = raw.get("order")
    if isinstance(order, dict) and isinstance(order.get(section_key), list):
        return [str(item) for item in order[section_key]]
    return None


def base_ids(raw: dict[str, Any]) -> dict[str, str]:
    ids = raw.get("ids")
    if isinstance(ids, dict):
        return {str(key): str(value) for key, value in ids.items()}
    return {}


def allocator_for(raw: dict[str, Any], previous_ids: dict[str, str] | None) -> IdAllocator:
    return IdAllocator(previous_ids if previous_ids is not None else base_ids(raw))


def claim_section_ids(section_guids: list[list[str]], known_ids: dict[str, str]) -> tuple[dict[str, str], set[str]]:
    """Claim the stored ids of every section, the first claimer in raw order winning.

    One asset shares a single id namespace across its sections, but ids are allocated
    section by section. Bookkeeping written before that rule can hand one id to two
    guids in different sections; the losing owner is left unclaimed here so its own
    section mints a suffixed id instead of shadowing the winner on push.
    """
    claimed: dict[str, str] = {}
    used: set[str] = set()
    for guids in section_guids:
        for guid in guids:
            identifier = known_ids.get(guid)
            if identifier is None or identifier in used:
                continue
            claimed[guid] = identifier
            used.add(identifier)
    return claimed, used


def section_allocator(guids: list[str], claimed: dict[str, str], used: set[str]) -> IdAllocator:
    """Allocator for one section, barred from reusing or re-minting ids of its siblings."""
    allocator = IdAllocator({guid: claimed[guid] for guid in guids if guid in claimed})
    for identifier in used:
        allocator.reserve(identifier)
    return allocator


def make_link(src: str, src_pin: str | None, dst: str, dst_pin: str | None) -> Link:
    return Link(src=src, src_pin=src_pin, dst=dst, dst_pin=dst_pin)


def make_decl(identifier: str, type_name: str, args: list[tuple[str | None, str]], pos: tuple[int, int] | None, meta: dict[str, Any]) -> Decl:
    return Decl(id=identifier, type_name=type_name, args=args, pos=pos, meta=meta)


def finish_document(document: Document, ids: dict[str, str], order: dict[str, list[str]]) -> dict[str, Any]:
    """Return the base-side bookkeeping stored next to the raw export."""
    return {"ids": ids, "order": order}


def require_kind(raw: dict[str, Any], *kinds: str) -> str:
    kind = str(raw.get("kind", ""))
    if kind not in kinds:
        raise RawError(f"unexpected raw kind {kind!r}, expected one of {kinds}")
    if int(raw.get("raw_version", 0)) != RAW_VERSION:
        raise RawError(f"unsupported raw_version {raw.get('raw_version')!r}")
    return kind
