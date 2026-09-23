"""Capture raw evidence and immutable editable semantic state separately."""

from __future__ import annotations

from ...codec import document_from_raw
from ...emitter import emit
from ...parser import parse
from ...scene.codec import from_raw as scene_from_raw
from ...sync_project import SyncError
from ..store.io import digest
from .decode import physical_ids, to_document
from .encode import encode
from .identity import plain, scene_metadata

VOLATILE = set(("dirty", "saved_hash", "exported_at", "generated_at", "timestamp", "revision", "live_revision", "content_revision", "editor_epoch", "request_token", "apply_id"))


def raw_evidence(raw: dict) -> dict:
    return dict((key, plain(value)) for key, value in raw.items() if key not in VOLATILE and key not in ("ids", "order", "aliases"))


def from_raw(raw: dict, previous: dict | None = None, namespace: str = "ue", schema=None) -> dict:
    kind = "scene" if raw.get("scene_version") or ("map_path" in raw and "actors" in raw) else raw["kind"]
    if kind == "scene":
        prior = physical_ids(previous) if previous else raw.get("aliases", dict())
        document, aliases = scene_from_raw(raw, prior)
        scene_metadata(document, aliases)
    else:
        previous_ids = physical_ids(previous) if previous else raw.get("ids")
        document, _, _ = document_from_raw(raw, previous_ids=previous_ids, schema=schema)
    semantic, bindings, _ = encode(document, kind, previous, namespace, schema)
    return dict(semantic=semantic, semantic_hash=digest(semantic), bindings=bindings,
                raw=raw_evidence(raw), schema_key=raw.get("schema_key", ""), codec_version=1)


def capture(text: str, previous: dict | None, namespace: str, kind: str, schema=None, file: str = "") -> dict:
    document, diagnostics = parse(text, file=file)
    if diagnostics.has_errors:
        raise SyncError("invalid_document", "\n".join(item.format() for item in diagnostics.errors()))
    if previous and document.header.asset != previous["semantic"]["header"]["asset"]:
        raise SyncError("asset_identity_changed", "file asset header differs from its registered identity")
    semantic, bindings, _ = encode(document, kind, previous, namespace, schema)
    return dict(semantic=semantic, semantic_hash=digest(semantic), bindings=bindings,
                raw=(previous or dict()).get("raw", dict()), schema_key=document.header.schema or (previous or dict()).get("schema_key", ""), codec_version=1)


def rebind(snapshot: dict | None, schema) -> dict | None:
    """The same mirror text, encoded the way ``schema`` reads it.

    Property types and defaults come from the schema, so two states recorded
    under different environments disagree about fields nobody edited. Comparing
    them reports the upgrade, not an edit, and refusing to compare them strands
    every workspace that was open when the plugin was rebuilt. The mirror is text:
    reading that text again under the current schema is exactly the state a fresh
    checkout would have recorded, with every identity kept.
    """
    if snapshot is None or schema is None or not schema.available or snapshot.get("schema_key") == schema.key:
        return snapshot
    document = to_document(snapshot)
    document.header.schema = schema.key
    semantic, bindings, _ = encode(document, snapshot["semantic"]["kind"], snapshot, "rebind", schema)
    return dict(snapshot, semantic=semantic, semantic_hash=digest(semantic), bindings=bindings, schema_key=schema.key)


def text_of(snapshot: dict) -> str:
    return emit(to_document(snapshot))
