"""Confined file selection and byte-preserving worktree capture."""

from __future__ import annotations

import re
from pathlib import Path

from ...parser import parse
from ...paths import object_path, package_name, parse_text_path, text_path
from ...scene.model import scene_name
from ...sync_project import SyncError
from ..semantic.snapshot import capture
from ..store.io import byte_hash, confined
from . import cache


def filename(asset: str, snapshot: dict) -> str:
    kind = snapshot["semantic"]["kind"]
    if kind != "scene":
        return text_path(Path("."), asset, kind).as_posix()
    map_path, _, name = asset.partition("#")
    safe = re.sub(r"[^\w.-]+", "_", name)
    return f"Scenes/{map_path.removeprefix('/Game/')}/{safe}.scene.nexus"


def discover(root: Path, registered: dict[str, str]) -> dict[str, str]:
    """Name the worktree's files; every reader and writer confines its own path.

    Walking the tree yields paths that are inside it by construction, and each
    consumer resolves what it is about to open. Resolving here as well doubled
    the syscalls a project-sized listing pays for nothing.
    """
    result = dict(registered)
    inverse = dict((relative, asset) for asset, relative in result.items())
    for path in sorted(root.rglob("*.nexus")):
        relative = path.relative_to(root).as_posix()
        if relative in inverse:
            continue
        if path.name.endswith(".scene.nexus"):
            document, sink = parse(path.read_text(encoding="utf-8"))
            if sink.has_errors:
                raise SyncError("invalid_document", f"invalid scene file: {path}")
            asset = package_name(document.header.asset) + "#" + scene_name(document)
        else:
            parsed = parse_text_path(root, path)
            if parsed is None:
                continue
            asset, _ = parsed
        if asset in result and result[asset] != relative:
            raise SyncError("duplicate_asset", f"two files represent {asset}")
        result[asset] = relative
    return result


def select(root: Path, files: dict[str, str], paths: list[str] | None) -> list[str]:
    if paths is None:
        return sorted(files)
    result: set[str] = set()
    for path in paths:
        if path.startswith("/Game/"):
            if "#" in path:
                matches = [asset for asset in files if asset == path]
            else:
                prefix = package_name(path).rstrip("/")
                matches = [asset for asset in files if package_name(asset.split("#")[0]) == prefix or asset.startswith(prefix + "/")]
        else:
            target = confined(root, path)
            matches = [asset for asset, file in files.items() if confined(root, file) == target or confined(root, file).is_relative_to(target)]
        if not matches:
            raise SyncError("path_not_found", f"path is not in this workspace: {path}")
        result.update(matches)
    return sorted(result)


def hashes(root: Path, files: dict[str, str]) -> dict[str, str | None]:
    return dict((asset, byte_hash(path.read_bytes()) if (path := confined(root, relative)).is_file() else None) for asset, relative in files.items())


def capture_files(workspace, paths: list[str] | None = None, delete: bool = False) -> tuple[dict, dict, dict]:
    state, history, store = workspace.state, workspace.history, workspace.store
    entries = history.entries(state["index"])
    registered = discover(workspace.root, state["files"])
    selected = select(workspace.root, registered, paths)
    memo, fresh, changed = cache.load(workspace), dict(), False
    captured_hashes = dict()
    for asset in selected:
        path = confined(workspace.root, registered[asset])
        if not path.is_file():
            captured_hashes[asset] = None
            if not delete:
                raise SyncError("local-deleted", "register file deletion explicitly with delete=true and allow_delete=true", dict(asset=asset, file=str(path)))
            entries.pop(asset, None)
            continue
        data = path.read_bytes()
        captured_hashes[asset] = byte_hash(data)
        previous_id = entries.get(asset)
        # Encoding is a pure function of these bytes and that base snapshot, so a
        # matching memo entry is the answer, not a guess about it.
        known = memo.get(asset)
        # An uncommitted result is reachable from nothing, so collection may have
        # taken it; the memo then has to earn its answer again.
        if known and known[:2] == [captured_hashes[asset], previous_id] and store.objects.exists(known[2]):
            entries[asset] = known[2]
            fresh[asset] = known
            continue
        prior = store.objects.data(previous_id, "snapshot") if previous_id else None
        kind = prior["semantic"]["kind"] if prior else ("scene" if path.name.endswith(".scene.nexus") else parse_text_path(workspace.root, path)[1])
        snapshot = capture(data.decode("utf-8-sig"), prior, f"{state['id']}:{state['head']}", kind, workspace.schema, str(path))
        if kind != "scene" and object_path(snapshot["semantic"]["header"]["asset"]) != asset:
            raise SyncError("asset_identity_changed", "asset header does not match its file path", dict(file=str(path)))
        entries[asset] = workspace.snapshot(snapshot)
        fresh[asset], changed = [captured_hashes[asset], previous_id, entries[asset]], True
    kept = dict((asset, value) for asset, value in memo.items() if asset in registered and asset not in fresh)
    fresh = (kept | fresh) if paths is not None else fresh
    if changed or len(fresh) != len(memo):
        cache.save(workspace, fresh)
    return entries, registered, captured_hashes
