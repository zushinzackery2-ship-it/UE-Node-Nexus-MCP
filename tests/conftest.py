from __future__ import annotations

from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.contracts import FEATURE_GROUPS


@pytest.fixture
def all_features(monkeypatch: pytest.MonkeyPatch) -> None:
    """Force every feature group enabled and skip CLI/env/probe resolution."""
    monkeypatch.setattr(runtime, "_enabled_features", set(FEATURE_GROUPS))
    monkeypatch.setattr(runtime, "_response_mode", "minimal")
    monkeypatch.setattr(runtime, "_profile_args_consumed", True)


@pytest.fixture
def use_bridge(monkeypatch: pytest.MonkeyPatch) -> Callable[[Any], Any]:
    def _install(bridge: Any) -> Any:
        from ue_node_nexus_mcp.instances.session import instance_manager
        from tests.support.scopes import Scope
        monkeypatch.setattr(runtime, "bridge", bridge)
        monkeypatch.setattr(instance_manager, "reserve", Scope)
        return bridge

    return _install
