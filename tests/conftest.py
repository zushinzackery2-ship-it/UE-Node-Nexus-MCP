from __future__ import annotations

from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.contracts import FEATURE_GROUPS


class RecordingBridge:
    """Fake bridge that answers every operation with one configurable response."""

    def __init__(self, response: dict[str, Any] | None = None) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.response: dict[str, Any] = response or {
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


class SequencedBridge:
    """Fake bridge that pops one prepared response per call, in order."""

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
    """Fake bridge that answers by operation name (dict or payload-callable)."""

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


@pytest.fixture
def all_features(monkeypatch: pytest.MonkeyPatch) -> None:
    """Force every feature group enabled and skip CLI/env/probe resolution."""
    monkeypatch.setattr(runtime, "_enabled_features", set(FEATURE_GROUPS))
    monkeypatch.setattr(runtime, "_response_mode", "minimal")
    monkeypatch.setattr(runtime, "_profile_args_consumed", True)


@pytest.fixture
def use_bridge(monkeypatch: pytest.MonkeyPatch) -> Callable[[Any], Any]:
    def _install(bridge: Any) -> Any:
        monkeypatch.setattr(runtime, "bridge", bridge)
        return bridge

    return _install
