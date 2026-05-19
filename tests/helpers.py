from __future__ import annotations

from typing import Any


class RecordingBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        self.calls.append((operation, payload))
        return {
            "ok": True,
            "operation": operation,
            "data": {},
            "diagnostics": [],
            "warnings": [],
        }
