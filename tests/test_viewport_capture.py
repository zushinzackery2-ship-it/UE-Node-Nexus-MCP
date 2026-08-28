"""Two-phase viewport screenshot tests on the live facade path."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import pytest

from ue_node_nexus_mcp.operation_registry import get_operation_spec
from ue_node_nexus_mcp.tools_facade import ue_execute

from conftest import RecordingBridge


def test_viewport_operations_carry_expected_metadata() -> None:
    capture = get_operation_spec("viewport_capture")
    assert (capture.kind, capture.group, capture.local_mcp) == ("write", "core", False)
    assert capture.bridge_operation == "viewport_capture"

    status = get_operation_spec("viewport_capture_status")
    assert (status.kind, status.local_mcp, status.bridge_operation) == ("read", True, None)


def test_capture_request_goes_to_bridge_unchanged(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(
        RecordingBridge(
            {
                "ok": True,
                "data": {"file_path": "C:/Proj/Saved/Screenshots/before_fix.png", "requested": True},
                "diagnostics": [],
                "warnings": [],
            }
        )
    )
    result = ue_execute("viewport_capture", {"filename": "before_fix", "show_ui": False})
    assert result["ok"] is True
    assert bridge.calls[0] == ("viewport_capture", {"filename": "before_fix", "show_ui": False})


def test_capture_status_reports_missing_then_present_file(all_features: None, use_bridge: Callable, tmp_path: Path) -> None:
    bridge = use_bridge(RecordingBridge())
    target = tmp_path / "shot.png"

    pending = ue_execute("viewport_capture_status", {"file_path": str(target)})
    assert pending["ok"] is True
    assert pending["data"]["exists"] is False

    target.write_bytes(b"\x89PNG fake body")
    done = ue_execute("viewport_capture_status", {"file_path": str(target)})
    assert done["data"]["exists"] is True
    assert done["data"]["size_bytes"] == target.stat().st_size

    # Status is MCP-local: the bridge must never be touched.
    assert bridge.calls == []


@pytest.mark.parametrize("payload", [{}, {"file_path": ""}, {"file_path": 7}])
def test_capture_status_rejects_bad_file_path(all_features: None, use_bridge: Callable, payload: dict) -> None:
    use_bridge(RecordingBridge())
    result = ue_execute("viewport_capture_status", payload)
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_operation"
