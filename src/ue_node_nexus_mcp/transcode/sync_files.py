"""Mirror file IO: text/base read-write, undo copies, canonical hashing."""

from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .codec import bookkeeping_from_base, document_from_raw, with_bookkeeping
from .emitter import emit
from .model import Document
from .parser import parse
from .paths import base_path, display_path, iter_text_files, parse_text_path, text_path, undo_dir
from .state import sha256_text

UNDO_KEEP = 10


def read_text(path: Path) -> str | None:
    if not path.is_file():
        return None
    return path.read_text(encoding="utf-8")


def read_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def write_json(path: Path, data: dict[str, Any]) -> None:
    write_text_atomic(path, json.dumps(data, indent=1, ensure_ascii=False))


def canonical_hash(text: str) -> str:
    """Hash of the canonical form so whitespace-only edits do not count as changes."""
    document, sink = parse(text)
    if sink.has_errors:
        return sha256_text(text)
    return sha256_text(emit(document))


def backup_text(project: Path, path: Path, stamp: str | None = None) -> None:
    if not path.is_file():
        return
    stamp = stamp or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    target = undo_dir(project) / stamp / display_path(project, path)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
    prune_undo(project)


def prune_undo(project: Path, keep: int = UNDO_KEEP) -> None:
    root = undo_dir(project)
    if not root.is_dir():
        return
    stamps = sorted((path for path in root.iterdir() if path.is_dir()), key=lambda path: path.name)
    for stale in stamps[:-keep] if len(stamps) > keep else []:
        shutil.rmtree(stale, ignore_errors=True)


def write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(text, encoding="utf-8", newline="\n")
    temp.replace(path)


def load_base(project: Path, asset_path: str) -> dict[str, Any] | None:
    return read_json(base_path(project, asset_path))


def base_document(base: dict[str, Any]) -> Document:
    ids, order = bookkeeping_from_base(base)
    document, _, _ = document_from_raw(base, ids, order)
    return document


@dataclass
class MirrorSnapshot:
    document: Document
    text: str
    text_file: Path
    base_file: Path
    stored: dict[str, Any]


def render_snapshot(project: Path, raw: dict[str, Any], previous: dict[str, Any] | None, order_override: dict[str, list[str]] | None = None, extra_ids: dict[str, str] | None = None) -> MirrorSnapshot:
    """Render text and bookkeeping without changing the accepted mirror."""
    ids, order = bookkeeping_from_base(previous)
    if extra_ids:
        ids = {**(ids or {}), **extra_ids}
    if order_override:
        order = {**(order or {}), **order_override}
    document, new_ids, new_order = document_from_raw(raw, ids, order)
    text = emit(document)
    kind = str(raw.get("kind", ""))
    asset_path = str(raw.get("asset_path", ""))
    text_file = text_path(project, asset_path, kind)
    base_file = base_path(project, asset_path)
    stored = with_bookkeeping(raw, new_ids, new_order)
    stored["text_sha256"] = sha256_text(text)
    return MirrorSnapshot(document, text, text_file, base_file, stored)


def materialize(project: Path, raw: dict[str, Any], previous: dict[str, Any] | None, order_override: dict[str, list[str]] | None = None, extra_ids: dict[str, str] | None = None) -> tuple[Document, str, Path, Path]:
    """raw -> (document, text, text_path, base_path); writes base with bookkeeping."""
    snapshot = render_snapshot(project, raw, previous, order_override, extra_ids)
    write_json(snapshot.base_file, snapshot.stored)
    return snapshot.document, snapshot.text, snapshot.text_file, snapshot.base_file


def local_order_from_document(document: Document) -> dict[str, list[str]]:
    """Order bookkeeping derived from a hand-edited document (keeps the agent's layout)."""
    order: dict[str, list[str]] = {}
    for section in document.sections:
        ids = [decl.id for decl in section.decls() if decl.modifier is None]
        if not ids:
            continue
        if section.name == "graph" and not section.args.strip():
            order["graph"] = ids
        elif section.name == "graph":
            order[f"graph:{section.args.strip()}"] = ids
        elif section.name == "function":
            name = section.args.strip().split("(", 1)[0]
            order[f"graph:{name}"] = ids
        elif section.name == "stack":
            order[f"stack:{section.args.strip()}"] = ids
    return order


def mirrored_assets(project: Path) -> dict[str, tuple[str, Path]]:
    """object path -> (kind, text path) for every mirror file in the project."""
    result: dict[str, tuple[str, Path]] = {}
    for path in iter_text_files(project):
        parsed = parse_text_path(project, path)
        if parsed is not None:
            result[parsed[0]] = (parsed[1], path)
    return result


def remove_stale_text(project: Path, asset_path: str, keep_kind: str) -> list[Path]:
    """When an asset changes kind (rare), drop other-suffix files for the same asset."""
    removed: list[Path] = []
    for kind, path in _all_kind_paths(project, asset_path):
        if kind != keep_kind and path.is_file():
            backup_text(project, path)
            path.unlink()
            removed.append(path)
    return removed


def _all_kind_paths(project: Path, asset_path: str) -> list[tuple[str, Path]]:
    from .paths import KIND_SUFFIX

    return [(kind, text_path(project, asset_path, kind)) for kind in KIND_SUFFIX]
