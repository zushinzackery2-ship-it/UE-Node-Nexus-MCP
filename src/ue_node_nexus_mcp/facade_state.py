from __future__ import annotations

import time
import json
from dataclasses import dataclass
from itertools import count
from typing import Any

from .facade_cache.retention import Retention


@dataclass
class StoredArtifact:
    artifact_id: str
    kind: str
    payload: dict[str, Any]
    created_at: float
    expires_at: float
    # Serialization is retained and budgeted once when the artifact is admitted.
    encoded: str | None = None


@dataclass
class StoredDiff:
    diff_token: str
    operation: str
    payload: dict[str, Any]
    response: dict[str, Any]
    created_at: float
    expires_at: float


class FacadeState:
    def __init__(self, ttl_seconds: int = 600, max_entries: int = 4096,
                 max_bytes: int = 256 * 1024 * 1024, clock=None) -> None:
        self.ttl_seconds = ttl_seconds
        self._counter = count(1)
        self._cache = Retention(ttl_seconds, max_entries, max_bytes, clock)

    def _next_id(self, prefix: str) -> str:
        return f"{prefix}_{next(self._counter)}"

    def _expires_at(self) -> float:
        return time.time() + self.ttl_seconds

    def statistics(self) -> dict:
        return self._cache.statistics()

    def store_artifact(self, kind: str, payload: dict[str, Any]) -> StoredArtifact:
        artifact_id = self._next_id("artifact")
        artifact = StoredArtifact(
            artifact_id=artifact_id,
            kind=kind,
            payload=payload,
            created_at=time.time(),
            expires_at=self._expires_at(),
            encoded=json.dumps(payload, ensure_ascii=True, separators=(",", ":"), default=str),
        )
        self._cache.put(artifact_id, artifact)
        return artifact

    def get_artifact(self, artifact_id: str) -> StoredArtifact | None:
        result = self._cache.get(artifact_id)
        return result if isinstance(result, StoredArtifact) else None

    def store_diff(self, operation: str, payload: dict[str, Any], response: dict[str, Any]) -> StoredDiff:
        diff_token = self._next_id("diff")
        diff = StoredDiff(
            diff_token=diff_token,
            operation=operation,
            payload=payload,
            response=response,
            created_at=time.time(),
            expires_at=self._expires_at(),
        )
        self._cache.put(diff_token, diff)
        return diff

    def get_diff(self, diff_token: str) -> StoredDiff | None:
        result = self._cache.get(diff_token)
        return result if isinstance(result, StoredDiff) else None


facade_state = FacadeState()
