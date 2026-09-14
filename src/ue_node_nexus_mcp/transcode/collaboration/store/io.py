"""Canonical objects and durable atomic file replacement."""

from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
from pathlib import Path
from typing import Any

from ...sync_project import SyncError

OBJECT_ID = re.compile(r"^[0-9a-f]{64}$")
# Object files fan out over 256 fixed directories, so a write batch asked the
# filesystem to create the same handful of them once per object. Remember which
# ones this process already made; a missing one is rediscovered on first use.
_DIRECTORIES: set[str] = set()


def ensure_directory(path: Path) -> None:
    key = str(path)
    if key not in _DIRECTORIES:
        path.mkdir(parents=True, exist_ok=True)
        _DIRECTORIES.add(key)


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")


def digest(value: Any) -> str:
    return hashlib.sha256(canonical(value)).hexdigest()


def byte_hash(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    ensure_directory(path.parent)
    try:
        fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    except FileNotFoundError:
        _DIRECTORIES.discard(str(path.parent))
        ensure_directory(path.parent)
        fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        if os.name != "nt":
            directory = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_bytes())
    except (OSError, ValueError) as exc:
        raise SyncError("store_corrupt", f"cannot read {path}: {exc}", dict(path=str(path))) from exc


_ROOTS: dict[str, Path] = dict()


def anchor(root: Path) -> Path:
    """Resolve a containing root once; it is the same answer for every member."""
    key = os.path.normcase(str(root))
    resolved = _ROOTS.get(key)
    if resolved is None:
        resolved = _ROOTS[key] = root.resolve()
    return resolved


def confined(root: Path, relative: str) -> Path:
    base = anchor(root)
    candidate = (base / relative).resolve()
    if not candidate.is_relative_to(base) or candidate == base:
        raise SyncError("invalid_path", "path escapes its registered workspace", dict(path=relative))
    return candidate
