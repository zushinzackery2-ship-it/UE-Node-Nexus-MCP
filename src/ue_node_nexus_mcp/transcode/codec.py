"""Kind dispatch: raw export -> Document (+ id/order bookkeeping)."""

from __future__ import annotations

from typing import Any

from .model import Document
from .raw_blueprint import blueprint_document
from .raw_common import RawError, base_ids, base_order
from .raw_material import material_document
from .raw_niagara import niagara_document
from .raw_simple import generic_asset_document, material_instance_document, stub_document
from .sync_project import SyncError

GRAPH_KINDS = ("material", "material_function", "blueprint", "niagara_system", "niagara_emitter")
ALL_KINDS = (*GRAPH_KINDS, "material_instance", "asset", "stub")


def raw_kind(raw: dict[str, Any]) -> str:
    return str(raw.get("kind", ""))


def document_from_raw(
    raw: dict[str, Any],
    previous_ids: dict[str, str] | None = None,
    previous_order: dict[str, list[str]] | None = None,
    schema=None,
) -> tuple[Document, dict[str, str], dict[str, list[str]]]:
    """Convert a raw export to a Document.

    ``previous_ids`` / ``previous_order`` come from the last base file so ids
    and hand-written ordering survive re-pulls. When omitted, bookkeeping
    embedded in ``raw`` itself (``ids`` / ``order``) is used. ``schema`` lets a
    reference that only the catalog can disambiguate reach the text in full.
    """
    kind = raw_kind(raw)
    try:
        return _build_document(raw, kind, previous_ids, previous_order, schema)
    except RawError as exc:
        raise SyncError("invalid_raw", str(exc), dict(kind=kind, asset=str(raw.get("asset_path", "")))) from exc


def _build_document(
    raw: dict[str, Any],
    kind: str,
    previous_ids: dict[str, str] | None,
    previous_order: dict[str, list[str]] | None,
    schema,
) -> tuple[Document, dict[str, str], dict[str, list[str]]]:
    if kind in ("material", "material_function"):
        return material_document(raw, previous_ids, previous_order)
    if kind == "blueprint":
        return blueprint_document(raw, previous_ids, previous_order)
    if kind in ("niagara_system", "niagara_emitter"):
        return niagara_document(raw, previous_ids, previous_order, schema)
    if kind == "material_instance":
        return material_instance_document(raw), {}, {}
    if kind == "asset":
        return generic_asset_document(raw), {}, {}
    if kind == "stub":
        return stub_document(raw), {}, {}
    raise RawError(f"unsupported raw kind: {kind!r}")


def with_bookkeeping(raw: dict[str, Any], ids: dict[str, str], order: dict[str, list[str]]) -> dict[str, Any]:
    """Return the raw dict enriched with the id/order bookkeeping for storage."""
    stored = dict(raw)
    stored["ids"] = ids
    stored["order"] = order
    return stored


def bookkeeping_from_base(base: dict[str, Any] | None) -> tuple[dict[str, str] | None, dict[str, list[str]] | None]:
    if not base:
        return None, None
    ids = base_ids(base)
    order: dict[str, list[str]] = {}
    raw_order = base.get("order")
    if isinstance(raw_order, dict):
        for key in raw_order:
            values = base_order(base, key)
            if values is not None:
                order[key] = values
    return ids, order
