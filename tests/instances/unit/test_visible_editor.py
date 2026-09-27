"""Window defaults and public close completion are observable contracts."""

from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle import engine
from ue_node_nexus_mcp.instances.session.shutdown import close_instance
from tests.instances.unit.conftest import Client


def test_new_managed_editor_uses_the_windowed_executable(tmp_path, monkeypatch):
    executable = tmp_path / "Engine/Binaries/Win64/UnrealEditor.exe"
    executable.parent.mkdir(parents=True)
    executable.touch()
    monkeypatch.setattr(engine, "resolve_engine", lambda *args: tmp_path)
    monkeypatch.setattr(engine, "verify", lambda *args: None)
    options = engine.launch_options(dict(project_path="Test.uproject"), dict())
    assert options["launch_profile"] == "interactive"
    assert options["executable"].endswith("unrealeditor.exe")
    assert options["rhi"] == "d3d12"
    with pytest.raises(InstanceError) as failure:
        engine.launch_options(dict(project_path="Test.uproject"), dict(rhi="nullrhi"))
    assert failure.value.details["field"] == "rhi"


def test_mcp_cannot_start_an_invisible_editor():
    with pytest.raises(InstanceError) as failure:
        engine.launch_options(dict(project_path="Test.uproject"), dict(launch_profile="offscreen"))
    assert failure.value.details["accepted"] == ["interactive"]


@pytest.mark.parametrize("profile,rhi", [("offscreen", "d3d12"), ("interactive", "nullrhi")])
def test_implicit_reuse_does_not_hide_an_old_offscreen_editor(profile, rhi):
    item = dict(guard_protocol=1, contract_version=4, launch_profile=profile, rhi=rhi)
    with pytest.raises(InstanceError, match="display profile"):
        engine.compatible(item, dict())
    # An exact selection remains available to inspect and close old instances.
    engine.compatible(item, dict(instance_id="old"))


class ClosingSession:
    def __init__(self, states):
        self.states = iter(states)
        self.calls = []
        self.lease = dict(lease_id="lease")
        self.broker = SimpleNamespace(policy=dict(shutdown_seconds=1))

    def current(self):
        return dict(instance_id="editor")

    def call(self, action, payload):
        self.calls.append((action, payload))
        return dict(instance_id="editor", state="DRAINING", operation_id="close")

    def status(self, payload):
        return dict(instance_id="editor", **next(self.states))


def test_close_waits_for_the_os_process_to_exit(monkeypatch):
    monkeypatch.setattr("ue_node_nexus_mcp.instances.session.shutdown.time.sleep", lambda seconds: None)
    session = ClosingSession([dict(state="STOPPING"), dict(state="EXITED", exit_code=0)])
    result = close_instance(session, dict(instance_id="editor", dry_run=False))
    assert result["state"] == "EXITED" and result["exit_confirmed"]
    assert result["exit_code"] == 0 and session.lease == dict()


def test_close_surfaces_dirty_blockers_instead_of_claiming_exit():
    session = ClosingSession([dict(state="BLOCKED", blockers=["instance_dirty"], dirty_packages=["/Game/Draft"])])
    with pytest.raises(InstanceError) as failure:
        close_instance(session, dict(instance_id="editor", dry_run=False))
    assert failure.value.code == "close_blocked"
    assert failure.value.details["instance"]["dirty_packages"] == ["/Game/Draft"]
    assert failure.value.details["exit_confirmed"] is False


def test_async_close_explicitly_reports_that_exit_is_unconfirmed():
    session = ClosingSession([])
    result = close_instance(session, dict(instance_id="editor", dry_run=False, wait=False))
    assert result["state"] == "DRAINING" and result["exit_confirmed"] is False
    assert "wait" not in session.calls[0][1]


def test_interactive_pin_does_not_invalidate_lifecycle_proposals(lifecycle):
    service, _, _, project = lifecycle
    client = Client(service)
    result = client.require("ensure", project_path=str(project), mode="reuse_or_start",
                            launch_profile="interactive", dry_run=False)
    identifier = result["instance"]["instance_id"]
    with service.lock:
        before = service.instances[identifier]["generation"]
    hold = client.require("pin", instance_id=identifier, seconds=60, reason="legacy client")
    assert hold["effective"] is False and hold["requires_renewal"] is False
    with service.lock:
        assert service.instances[identifier]["generation"] == before
        assert "pin_until" not in service.instances[identifier]


def test_editor_process_launch_preserves_the_visible_window(tmp_path, monkeypatch):
    from ue_node_nexus_mcp.instances.lifecycle import platform

    calls = []
    process = SimpleNamespace(pid=123, poll=lambda: None)

    def launch(command, **options):
        calls.append((command, options))
        return process

    monkeypatch.setattr(platform.subprocess, "Popen", launch)
    native = platform.WindowsPlatform(tmp_path / "Runtime")
    monkeypatch.setattr(native, "inspect", lambda pid: dict(pid=pid, process_created="1", executable="UnrealEditor.exe"))
    instance = dict(instance_id="editor", start_intent_id="intent", project_path=str(tmp_path / "Test.uproject"),
                    launch=dict(executable="UnrealEditor.exe", engine_dir="engine", launch_profile="interactive", rhi="d3d12"))
    try:
        native.launch(instance)
        command, options = calls[0]
        assert options["creationflags"] == 0
        assert not set(command) & set(("-RenderOffscreen", "-unattended", "-nosound"))
        assert command[0] == "UnrealEditor.exe"
    finally:
        native.close()


def test_pin_is_only_in_explicit_legacy_discovery():
    from ue_node_nexus_mcp.server import ue_capability_get

    visible = [row[0] for row in ue_capability_get()["data"]["operations"]]
    all_operations = [row[0] for row in ue_capability_get(include_hidden=True)["data"]["operations"]]
    assert "bridge_instance_pin" not in visible
    assert "bridge_instance_pin" in all_operations


def test_exited_status_clears_persisted_window_facts(lifecycle):
    service, _, _, project = lifecycle
    client = Client(service)
    result = client.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    identifier = result["instance"]["instance_id"]
    with service.lock:
        item = service.instances[identifier]
        item.update(state="EXITED", ready=False, window_visible=True, window_titles=["Old editor"])
    result = client.require("status", instance_id=identifier)
    assert result["window_visible"] is False
    assert result["window_titles"] == []
