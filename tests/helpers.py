from __future__ import annotations

from typing import Any


class RecordingBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.response: dict[str, Any] = {
            "ok": True,
            "operation": "",
            "data": {},
            "diagnostics": [],
            "warnings": [],
        }

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        response = dict(self.response)
        response["operation"] = response.get("operation") or operation
        return response
