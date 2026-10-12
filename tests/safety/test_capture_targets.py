"""Public screenshot selection must preserve the requested rendering surface."""

import pytest

from ue_node_nexus_mcp.payload_schema import payload_schema_for
from ue_node_nexus_mcp import tools_viewport


def test_capture_schema_exposes_explicit_pie():
    target = payload_schema_for("viewport_capture")["properties"]["target"]
    assert target["enum"] == ["level", "active", "pie"]
    assert target["default"] == "level"


@pytest.mark.parametrize("target", ("level", "active", "pie"))
def test_capture_forwards_the_selected_surface(monkeypatch, target):
    calls = []

    def call(operation, payload):
        calls.append((operation, payload))
        return dict(ok=True, data=dict(target=payload["target"]))

    monkeypatch.setattr(tools_viewport, "_call", call)
    result = tools_viewport.viewport_capture(target=target, dry_run=True)
    assert result["data"]["target"] == target
    assert calls[0][0] == "viewport_capture"
    assert calls[0][1]["dry_run"] is True


@pytest.mark.parametrize("target", ("PIE", "game", "", None))
def test_invalid_capture_target_is_refused_before_transport(monkeypatch, target):
    def unexpected(*_args, **_kwargs):
        pytest.fail("invalid selection reached the editor")

    monkeypatch.setattr(tools_viewport, "_call", unexpected)
    with pytest.raises(ValueError, match="level/active/pie"):
        tools_viewport.viewport_capture(target=target)
