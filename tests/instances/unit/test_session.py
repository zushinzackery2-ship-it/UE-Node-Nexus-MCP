import time

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from tests.instances.support.sdk import LocalBroker


@pytest.fixture
def session(lifecycle):
    service, _, _, project = lifecycle
    result = EditorSession(LocalBroker(service, project.parent), str(project))
    result.ensure(dict(mode="reuse_or_start", dry_run=False))
    ready(result, service)
    yield result
    result.close()


def ready(session, service):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        service.reconcile()
        if session.status()["state"] == "READY":
            return
        time.sleep(0.005)
    pytest.fail("injected editor did not become ready")


def test_idle_reacquire_preserves_authorized_project_restart(session, lifecycle):
    service, platform, clock, project = lifecycle
    identifier = session.current()["instance_id"]
    for _ in range(21):
        clock.advance(15)
        service.tick()
    assert not service.sessions.leases
    with session.work_scope("asset_get"):
        assert not session.explicit_instance
        assert session.options["mode"] == "reuse_or_start"
    platform.records.pop(identifier)
    service.editors.refresh()
    with pytest.raises(InstanceError) as failure:
        session.reserve("asset_get")
    assert failure.value.code == "instance_starting"
    ready(session, service)
    with session.work_scope("asset_get") as scope:
        assert scope.instance["instance_id"] != identifier
        assert scope.instance["project_path"] == session.current()["project_path"]


def test_explicit_instance_never_switches_after_exit(session, lifecycle):
    service, platform, _, project = lifecycle
    old = session.current()
    session.select(instance_id=old["instance_id"])
    platform.records.pop(old["instance_id"])
    service.editors.refresh()
    for _ in range(2):
        with pytest.raises(InstanceError) as failure:
            session.reserve("asset_get")
        assert failure.value.code == "stale_instance"
        assert session.current()["instance_id"] == old["instance_id"]
    assert platform.spawn_count == 1


def test_reserved_task_remains_on_original_instance_after_selection_changes(session, lifecycle):
    service, _, _, project = lifecycle
    queued = session.reserve("asset_get", wait=False)
    first = queued.instance["instance_id"]
    other = project.with_name("Other.uproject")
    other.touch()
    session.ensure(dict(project_path=str(other), mode="reuse_or_start", dry_run=False))
    ready(session, service)
    assert session.current()["instance_id"] != first
    with queued.activate():
        with session.work_scope("asset_get") as work:
            assert work.metadata()["instance_id"] == first
    queued.release()


def test_closed_session_does_not_restart_background_heartbeat(session):
    session.close()
    with pytest.raises(InstanceError) as failure:
        session.status()
    assert failure.value.code == "session_closed"
    assert not session.thread.is_alive()


def test_feature_observation_invalidates_cached_capabilities(session):
    changed = []
    session.set_on_bind_changed(lambda: changed.append(True))
    identifier = session.current()["instance_id"]
    session.observe(dict(instance_id=identifier, vfx_available=False))
    session.observe(dict(instance_id=identifier, vfx_available=True))
    session.observe(dict(instance_id=identifier, vfx_available=True))
    assert len(changed) == 2


def test_public_list_exposes_the_effective_shared_policy(session, lifecycle):
    from ue_node_nexus_mcp.instances.tools.dispatch import execute

    service, _, _, project = lifecycle
    response = execute("bridge_instance_list", dict(project_path=str(project)), session)
    assert response["ok"] and response["data"]["policy"] == service.policy_dict()
    assert len(response["data"]["instances"]) == 1
