"""A pending module observation must not become a permanent disabled feature."""

import pytest

from ue_node_nexus_mcp import runtime


@pytest.fixture
def observation(monkeypatch):
    current = {"state": "STARTING", "vfx_available": False}
    monkeypatch.setattr(runtime, "_enabled_features", None)
    monkeypatch.setattr(runtime, "_cli_feature_args", None)
    monkeypatch.setattr(runtime, "consume_cli_arguments", lambda: None)
    monkeypatch.setattr(runtime, "read_feature_env", lambda _: ({"core", "vfx"}, set(), set(), None))
    monkeypatch.setattr(runtime.instance_manager, "current", lambda: dict(current))
    return current


@pytest.mark.parametrize("state", ["STARTING", "WAITING_FOR_USER", "EXITED"])
def test_pending_availability_is_not_cached(observation, state):
    observation["state"] = state
    assert "vfx" not in runtime.enabled_features()
    assert runtime._enabled_features is None
    observation.update(state="READY", vfx_available=True)
    assert "vfx" in runtime.enabled_features()


def test_ready_disabled_module_is_definitive(observation):
    observation["state"] = "READY"
    assert "vfx" not in runtime.enabled_features()
    assert runtime._enabled_features == {"core"}


def test_rebinding_rechecks_a_definitive_observation(observation):
    observation["state"] = "READY"
    assert "vfx" not in runtime.enabled_features()
    observation["vfx_available"] = True
    runtime.reset_feature_cache()
    assert "vfx" in runtime.enabled_features()
