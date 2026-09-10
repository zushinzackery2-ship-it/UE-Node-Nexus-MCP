"""Deterministic bridge doubles shared across test packages."""

from __future__ import annotations

from typing import Any


class RecordingBridge:
    """Answer every operation with one configurable response."""

    def __init__(self, response: dict[str, Any] | None = None) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.response = response or dict(ok=True, operation="", data=dict(), diagnostics=[], warnings=[])

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        response = dict(self.response)
        response["operation"] = response.get("operation") or operation
        return response


class SequencedBridge:
    """Pop one prepared response per call, in order."""

    def __init__(self, responses: list[dict[str, Any]]) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self._responses = responses

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        response = dict(self._responses.pop(0))
        response["operation"] = response.get("operation") or operation
        response.setdefault("diagnostics", [])
        response.setdefault("warnings", [])
        return response


class RoutingBridge:
    """Answer by operation name with a response or payload-callable."""

    def __init__(self, routes: dict[str, Any]) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self._routes = routes

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        route = self._routes[operation]
        response = dict(route(payload) if callable(route) else route)
        response.setdefault("operation", operation)
        response.setdefault("diagnostics", [])
        response.setdefault("warnings", [])
        return response
