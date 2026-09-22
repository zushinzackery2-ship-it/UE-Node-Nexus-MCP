"""Serve stored artifacts within a hard per-response byte budget.

An artifact exists because a response was already too large to inline, so the
read that fetches it is exactly the call most likely to exceed the transport
frame. Returning the whole payload made the mechanism fail precisely in the
case it was built for: a conflict set big enough to need paging could not be
read at all, in one piece or in parts.
"""

from __future__ import annotations

import json
from typing import Any

from .facade_response import minimal_error
from .facade_state import StoredArtifact

# Comfortably inside any MCP transport frame, with room for the envelope the
# host wraps around this payload.
ARTIFACT_PAGE_BYTE_BUDGET = 4 * 1024 * 1024


def encode(artifact: StoredArtifact) -> str:
    """ASCII-only JSON, so a byte offset is always a safe character offset."""
    if artifact.encoded is None:
        artifact.encoded = json.dumps(artifact.payload, ensure_ascii=True, separators=(",", ":"), default=str)
    return artifact.encoded


def budget_of(query: dict[str, Any]) -> int | None:
    requested = query.get("limit_bytes", query.get("limit"))
    if requested is None:
        return ARTIFACT_PAGE_BYTE_BUDGET
    if isinstance(requested, bool) or not isinstance(requested, int) or requested <= 0:
        return None
    return min(requested, ARTIFACT_PAGE_BYTE_BUDGET)


def read(artifact: StoredArtifact, query: dict[str, Any]) -> dict[str, Any]:
    cursor = query.get("cursor", 0)
    if isinstance(cursor, bool) or not isinstance(cursor, int) or cursor < 0:
        return minimal_error("invalid_request", "cursor must be a nonnegative integer",
                             {"artifact_id": artifact.artifact_id, "cursor": query.get("cursor")})
    budget = budget_of(query)
    if budget is None:
        return minimal_error("invalid_request", "limit_bytes must be a positive integer",
                             {"artifact_id": artifact.artifact_id, "max_limit_bytes": ARTIFACT_PAGE_BYTE_BUDGET})
    encoded = encode(artifact)
    total = len(encoded)
    if cursor == 0 and total <= budget:
        return {"ok": True, "data": artifact.payload}
    if cursor > total:
        return minimal_error("invalid_request", "cursor is past the end of this artifact",
                             {"artifact_id": artifact.artifact_id, "cursor": cursor, "total_bytes": total})
    chunk = encoded[cursor:cursor + budget]
    end = cursor + len(chunk)
    return {
        "ok": True,
        "data": {
            "artifact_id": artifact.artifact_id,
            "kind": artifact.kind,
            # The payload does not fit one response, so it is delivered as the
            # JSON text itself: concatenate every chunk in cursor order, then parse.
            "encoding": "json-text",
            "chunk": chunk,
            "cursor": cursor,
            "next_cursor": end if end < total else None,
            "chunk_bytes": len(chunk),
            "total_bytes": total,
            "truncated": end < total,
            "next_read": None if end >= total else {
                "tool": "ue_read",
                "args": {"target": "artifact", "query": {"artifact_id": artifact.artifact_id, "cursor": end}},
            },
        },
    }
