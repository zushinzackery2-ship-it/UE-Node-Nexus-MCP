"""Serve stored artifacts within a per-response byte budget.

An artifact exists because a response was already too large to inline, so the
read that fetches it is exactly the call most likely to exceed what the caller
can take. Two limits apply. The transport frame is the hard ceiling; the
caller's context is the one that bites first: a 3 MB page fits any frame and is
still unreadable for an agent. So a page defaults to an agent-sized budget, and
a list inside the payload pages by whole items instead of by bytes of JSON text.
"""

from __future__ import annotations

import json
from typing import Any

from .facade_response import minimal_error
from .facade_state import StoredArtifact

# Comfortably inside any MCP transport frame, with room for the envelope the
# host wraps around this payload. A caller may ask for up to this much.
ARTIFACT_PAGE_BYTE_BUDGET = 4 * 1024 * 1024
# What one read returns unless the caller asks for more: a few times the inline
# response limit, well under the tool-output limits of agent clients.
ARTIFACT_PAGE_DEFAULT_BYTES = 48 * 1024
QUERY_FIELDS = ("artifact_id", "cursor", "limit_bytes", "limit", "path")
# Lists longer than this are named in a text page so the caller can page them.
LIST_HINT_MINIMUM = 2


def encode(value: Any) -> str:
    """ASCII-only JSON, so a byte offset is always a safe character offset."""
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"), default=str)


def encoded(artifact: StoredArtifact) -> str:
    if artifact.encoded is None:
        artifact.encoded = encode(artifact.payload)
    return artifact.encoded


def budget_of(query: dict[str, Any]) -> int | None:
    requested = query.get("limit_bytes", query.get("limit"))
    if requested is None:
        return min(ARTIFACT_PAGE_DEFAULT_BYTES, ARTIFACT_PAGE_BYTE_BUDGET)
    if isinstance(requested, bool) or not isinstance(requested, int) or requested <= 0:
        return None
    return min(requested, ARTIFACT_PAGE_BYTE_BUDGET)


def locate(payload: Any, path: Any) -> tuple[Any, list, dict | None]:
    """The value at a dotted path (``data.conflicts``) and the keys it took."""
    keys = path.split(".") if isinstance(path, str) else list(path) if isinstance(path, list) else None
    if not keys or not all(isinstance(key, (str, int)) and str(key) for key in keys):
        return None, [], {"reason": "path must be a dotted string such as \"data.conflicts\""}
    value, walked = payload, []
    for key in keys:
        if isinstance(value, dict) and str(key) in value:
            value = value[str(key)]
        elif isinstance(value, list) and str(key).isdigit() and int(key) < len(value):
            value = value[int(key)]
        else:
            available = sorted(value)[:50] if isinstance(value, dict) else f"0..{len(value) - 1}" if isinstance(value, list) else None
            return None, walked, {"reason": f"no {key!r} under {'.'.join(map(str, walked)) or 'the payload'}", "available": available}
        walked.append(key)
    return value, walked, None


def lists_in(value: Any, prefix: str = "", depth: int = 0) -> dict[str, int]:
    """Paths of the lists a caller would want to page item by item."""
    found: dict[str, int] = {}
    if isinstance(value, dict) and depth < 3:
        for key, item in value.items():
            path = f"{prefix}{key}"
            if isinstance(item, list) and len(item) >= LIST_HINT_MINIMUM:
                found[path] = len(item)
            elif isinstance(item, dict):
                found.update(lists_in(item, path + ".", depth + 1))
    return found


def read(artifact: StoredArtifact, query: dict[str, Any]) -> dict[str, Any]:
    unknown = sorted(set(query) - set(QUERY_FIELDS))
    if unknown:
        return minimal_error("unknown_query_field", f"artifact reads do not accept {', '.join(unknown)}",
                             {"artifact_id": artifact.artifact_id, "unknown": unknown, "allowed": list(QUERY_FIELDS)})
    cursor = query.get("cursor", 0)
    if isinstance(cursor, bool) or not isinstance(cursor, int) or cursor < 0:
        return minimal_error("invalid_request", "cursor must be a nonnegative integer",
                             {"artifact_id": artifact.artifact_id, "cursor": query.get("cursor")})
    budget = budget_of(query)
    if budget is None:
        return minimal_error("invalid_request", "limit_bytes must be a positive integer",
                             {"artifact_id": artifact.artifact_id, "max_limit_bytes": ARTIFACT_PAGE_BYTE_BUDGET})
    if query.get("path") is None:
        return text_page(artifact, artifact.payload, encoded(artifact), None, cursor, budget)
    value, walked, problem = locate(artifact.payload, query["path"])
    if problem is not None:
        return minimal_error("invalid_request", problem["reason"], {"artifact_id": artifact.artifact_id, **problem})
    path = ".".join(map(str, walked))
    if isinstance(value, list):
        return item_page(artifact, value, path, cursor, budget)
    return text_page(artifact, value, encode(value), path, cursor, budget)


def follow(artifact_id: str, path: str | None = None, cursor: int = 0, budget: int | None = None) -> dict[str, Any]:
    """The ``ue_read`` call that returns one page of an artifact."""
    query: dict[str, Any] = {"artifact_id": artifact_id, "cursor": cursor}
    if path is not None:
        query["path"] = path
    if budget is not None and budget != min(ARTIFACT_PAGE_DEFAULT_BYTES, ARTIFACT_PAGE_BYTE_BUDGET):
        query["limit_bytes"] = budget
    return {"tool": "ue_read", "args": {"target": "artifact", "query": query}}


def list_reads(artifact_id: str, payload: Any) -> dict[str, dict[str, Any]]:
    """For a response stored whole: the call that pages each of its lists by item."""
    return dict((path, follow(artifact_id, path)) for path in lists_in(payload))


def item_page(artifact: StoredArtifact, items: list, path: str, cursor: int, budget: int) -> dict[str, Any]:
    """Whole items from ``cursor`` while they fit; every page parses on its own."""
    if cursor > len(items):
        return minimal_error("invalid_request", "cursor is past the end of this list",
                             {"artifact_id": artifact.artifact_id, "path": path, "cursor": cursor, "total": len(items)})
    page, used = [], 2
    for item in items[cursor:]:
        size = len(encode(item)) + (1 if page else 0)
        if used + size > budget:
            break
        page.append(item)
        used += size
    end = cursor + len(page)
    data: dict[str, Any] = {"artifact_id": artifact.artifact_id, "kind": artifact.kind, "encoding": "items", "path": path,
                            "items": page, "cursor": cursor, "next_cursor": end if end < len(items) else None,
                            "total": len(items), "page_bytes": used, "truncated": end < len(items)}
    if not page and cursor < len(items):
        # One item larger than the page: it is readable, as text, under its own path.
        data.update(oversized_item={"index": cursor, "bytes": len(encode(items[cursor]))},
                    next_read=follow(artifact.artifact_id, f"{path}.{cursor}", 0, budget))
    else:
        data["next_read"] = follow(artifact.artifact_id, path, end, budget) if end < len(items) else None
    return {"ok": True, "data": data}


def text_page(artifact: StoredArtifact, value: Any, text: str, path: str | None, cursor: int, budget: int) -> dict[str, Any]:
    total = len(text)
    if cursor == 0 and total <= budget:
        return {"ok": True, "data": value} if path is None else {
            "ok": True, "data": {"artifact_id": artifact.artifact_id, "kind": artifact.kind, "encoding": "value", "path": path, "value": value}}
    if cursor > total:
        return minimal_error("invalid_request", "cursor is past the end of this artifact",
                             {"artifact_id": artifact.artifact_id, "cursor": cursor, "total_bytes": total})
    chunk = text[cursor:cursor + budget]
    end = cursor + len(chunk)
    data: dict[str, Any] = {
        "artifact_id": artifact.artifact_id,
        "kind": artifact.kind,
        # The value does not fit one response, so it is delivered as the JSON
        # text itself: concatenate every chunk in cursor order, then parse.
        "encoding": "json-text",
        "chunk": chunk,
        "cursor": cursor,
        "next_cursor": end if end < total else None,
        "chunk_bytes": len(chunk),
        "total_bytes": total,
        "truncated": end < total,
        "next_read": None if end >= total else follow(artifact.artifact_id, path, end, budget),
    }
    if path is not None:
        data["path"] = path
    if cursor == 0:
        # Reassembling text is the fallback; a list is better read item by item.
        prefix = f"{path}." if path else ""
        lists = {prefix + key: count for key, count in lists_in(value).items()}
        if lists:
            data["lists"] = lists
            data["page_lists_with"] = follow(artifact.artifact_id, next(iter(lists)), 0, budget)
    return {"ok": True, "data": data}
