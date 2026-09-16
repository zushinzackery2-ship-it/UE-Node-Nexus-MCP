import time

from ue_node_nexus_mcp.instances.broker.history import prune
from .conftest import Client


def test_history_retention_keeps_live_instances_and_completed_key_semantics(lifecycle, monkeypatch):
    from ue_node_nexus_mcp.instances.broker import history
    monkeypatch.setattr(history, "MAX_EXITED", 2)
    service, platform, _, project = lifecycle
    client = Client(service)
    request = dict(project_path=str(project), mode="reuse_or_start", idempotency_key="done", dry_run=False)
    first = client.require("ensure", **request)["instance"]["instance_id"]
    client.ready(project)
    client.require("release")
    platform.records.pop(first)
    service.editors.refresh()
    live = client.ready(project)["instance"]["instance_id"]
    for index in range(5):
        item = dict(service.instances[first], instance_id=f"old-{index}", project_key=f"old-project-{index}",
                    exited_wall_time=time.time() + index + 1)
        service.instances[item["instance_id"]] = item
        service.save(item)
    service.editors.refresh()
    prune(service)
    assert live in service.instances and first not in service.instances
    assert len(service.instances) == 3
    assert all(key in service.instances for members in service.projects.values() for key in members)
    assert len(service.registry.all("instance")) == 3
    assert client.call("ensure", **request)["error"]["code"] == "stale_instance"
    assert platform.spawn_count == 2


def test_close_preview_reports_unsaved_busy_and_recovery_state(lifecycle):
    service, _, _, project = lifecycle
    client = Client(service)
    identifier = client.ready(project)["instance"]["instance_id"]
    item = service.instances[identifier]
    item.update(dirty_packages=["/Game/A", "/Game/B"], pie=True, recovery_pending=["apply-example"])
    preview = client.require("close", instance_id=identifier)
    assert set(preview["blockers"]) == set(("instance_dirty", "pie_active", "recovery_pending"))
    preview = client.require("close", instance_id=identifier, save_packages=["/Game/A", "/Game/B"])
    assert set(preview["blockers"]) == set(("pie_active", "recovery_pending"))
    assert item["state"] == "READY" and item["dirty_packages"] == ["/Game/A", "/Game/B"]
