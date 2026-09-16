"""Reading historical logs must neither launch UE nor extend its use lease."""

import time

from ue_node_nexus_mcp import tools_system
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from tests.instances.support.sdk import LocalBroker
from .test_session import ready


def test_log_polling_is_offline_before_use_and_after_reclamation(lifecycle, monkeypatch):
    service, platform, clock, project = lifecycle
    session = EditorSession(LocalBroker(service, project.parent), str(project))
    logs = project.parent / "Saved/Nexus/Logs"
    logs.mkdir(parents=True)
    (logs / "instance.log").write_text("LogNexus: persisted evidence\n", encoding="utf-8")
    monkeypatch.setattr(tools_system, "instance_manager", session)

    def reject_bridge(*args, **kwargs):
        raise AssertionError("log observation must not call UE")

    monkeypatch.setattr(tools_system, "_call", reject_bridge)
    try:
        assert tools_system.log_tail_get()["data"]["text"] == "LogNexus: persisted evidence"
        assert platform.spawn_count == 0 and not service.sessions.clients
        session.ensure(dict(mode="reuse_or_start", dry_run=False))
        ready(session, service)
        identifier = session.current()["instance_id"]
        for _ in range(32):
            clock.advance(15)
            assert tools_system.log_tail_get()["ok"]
            service.tick()
        deadline = time.monotonic() + 3
        while not platform.closed and time.monotonic() < deadline:
            time.sleep(0.005)
        assert platform.closed == [identifier]
        assert tools_system.log_tail_get()["ok"]
        assert platform.spawn_count == 1 and not service.sessions.leases
    finally:
        session.close()


def test_offline_project_follows_explicit_selection(lifecycle):
    service, _, _, configured = lifecycle
    selected = configured.with_name("Selected.uproject")
    selected.touch()
    session = EditorSession(LocalBroker(service, configured.parent), str(configured))
    session.binding = dict(project_path=str(selected))
    assert session.project()["project_name"] == "selected"
    assert not service.sessions.clients
