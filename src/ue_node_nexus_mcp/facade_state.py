from __future__ import annotations

import time
from dataclasses import dataclass
from itertools import count
from typing import Any


@dataclass
class StoredArtifact:
    artifact_id: str
    kind: str
    payload: dict[str, Any]
    created_at: float
    expires_at: float


@dataclass
class StoredDiff:
    diff_token: str
    operation: str
    payload: dict[str, Any]
    response: dict[str, Any]
    created_at: float
    expires_at: float


class FacadeState:
    def __init__(self, ttl_seconds: int = 600) -> None:
        self.ttl_seconds = ttl_seconds
        self._counter = count(1)
        self.artifacts: dict[str, StoredArtifact] = {}
        self.diffs: dict[str, StoredDiff] = {}

    def _next_id(self, prefix: str) -> str:
        return f"{prefix}_{next(self._counter)}"

    def _expires_at(self) -> float:
        return time.time() + self.ttl_seconds

    def prune_expired(self) -> None:
        now = time.time()
        self.artifacts = {
            key: item
            for key, item in self.artifacts.items()
            if item.expires_at > now
        }
        self.diffs = {
            key: item
            for key, item in self.diffs.items()
            if item.expires_at > now
        }

    def store_artifact(self, kind: str, payload: dict[str, Any]) -> StoredArtifact:
        self.prune_expired()
        artifact_id = self._next_id("artifact")
        artifact = StoredArtifact(
            artifact_id=artifact_id,
            kind=kind,
            payload=payload,
            created_at=time.time(),
            expires_at=self._expires_at(),
        )
        self.artifacts[artifact_id] = artifact
        return artifact

    def get_artifact(self, artifact_id: str) -> StoredArtifact | None:
        self.prune_expired()
        return self.artifacts.get(artifact_id)

    def store_diff(self, operation: str, payload: dict[str, Any], response: dict[str, Any]) -> StoredDiff:
        self.prune_expired()
        diff_token = self._next_id("diff")
        diff = StoredDiff(
            diff_token=diff_token,
            operation=operation,
            payload=payload,
            response=response,
            created_at=time.time(),
            expires_at=self._expires_at(),
        )
        self.diffs[diff_token] = diff
        return diff

    def get_diff(self, diff_token: str) -> StoredDiff | None:
        self.prune_expired()
        return self.diffs.get(diff_token)


facade_state = FacadeState()
