from __future__ import annotations

from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import runtime

from tests.support.bridges import SequencedBridge

BUSY = {"ok": False, "error": {"code": "bridge_busy", "message": "another request is executing"}}
DONE = {"ok": True, "data": {"applied": True}}


def test_call_bridge_retries_while_editor_reports_busy(use_bridge: Callable, monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = use_bridge(SequencedBridge([dict(BUSY), dict(BUSY), dict(DONE)]))
    sleeps: list[float] = []
    monkeypatch.setattr(runtime.time, "sleep", sleeps.append)
    response = runtime.call_bridge("asset_save", {"asset_path": "/Game/A.A"})
    assert response["ok"] is True
    assert len(bridge.calls) == 3
    assert sleeps == [runtime._BUSY_RETRY_SECONDS] * 2


def test_call_bridge_gives_up_after_bounded_retries(use_bridge: Callable, monkeypatch: pytest.MonkeyPatch) -> None:
    bridge = use_bridge(SequencedBridge([dict(BUSY) for _ in range(runtime._BUSY_RETRIES + 1)]))
    monkeypatch.setattr(runtime.time, "sleep", lambda _: None)
    response = runtime.call_bridge("asset_save", {"asset_path": "/Game/A.A"})
    assert response["ok"] is False
    assert response["error"]["code"] == "bridge_busy"
    assert len(bridge.calls) == runtime._BUSY_RETRIES + 1


def test_call_bridge_does_not_retry_other_errors(use_bridge: Callable) -> None:
    failure: dict[str, Any] = {"ok": False, "error": {"code": "save_blocked_read_only", "message": "read-only on disk"}}
    bridge = use_bridge(SequencedBridge([failure]))
    response = runtime.call_bridge("asset_save", {"asset_path": "/Game/A.A"})
    assert response["error"]["code"] == "save_blocked_read_only"
    assert len(bridge.calls) == 1


def test_streaming_suspended_save_preserves_retry_contract(use_bridge: Callable) -> None:
    failure: dict[str, Any] = {
        "ok": False,
        "error": {
            "code": "save_blocked_asset_streaming_suspended",
            "message": "asset streaming is suspended",
        },
        "data": {
            "save_ready": False,
            "retryable": True,
        },
    }
    bridge = use_bridge(SequencedBridge([failure]))

    response = runtime.call_bridge("asset_save", {"asset_path": "/Game/A.A"})

    assert response["error"]["code"] == "save_blocked_asset_streaming_suspended"
    assert response["data"] == {"save_ready": False, "retryable": True}
    assert len(bridge.calls) == 1
