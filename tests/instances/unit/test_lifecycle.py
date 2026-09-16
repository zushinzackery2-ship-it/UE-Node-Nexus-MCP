from concurrent.futures import ThreadPoolExecutor
import time

from .conftest import Client


def test_concurrent_ensure_creates_one_instance_and_idempotent_leases(lifecycle):
    service, platform, clock, project = lifecycle
    clients = [Client(service) for _ in range(32)]
    with ThreadPoolExecutor(max_workers=32) as pool:
        results = list(pool.map(lambda client: client.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False), clients))
    assert len(set(item["instance"]["instance_id"] for item in results)) == 1
    clients[0].ready(project)
    assert platform.spawn_count == 1
    leases = clients[0].require("status", instance_id=results[0]["instance"]["instance_id"])["users"]
    assert len(leases) == 32


def test_heartbeat_does_not_pin_idle_editor(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    result = client.ready(project)
    for _ in range(32):
        clock.advance(15)
        client.require("heartbeat")
        client.require("status", instance_id=result["instance"]["instance_id"])
        service.tick()
    deadline = time.monotonic() + 3
    while not platform.closed and time.monotonic() < deadline:
        time.sleep(0.005)
    assert platform.closed == [result["instance"]["instance_id"]]


def test_one_client_cannot_close_another_clients_editor(lifecycle):
    service, platform, clock, project = lifecycle
    a, b = Client(service), Client(service)
    first = a.ready(project)
    b.ready(project)
    response = a.call("close", instance_id=first["instance"]["instance_id"], dry_run=False)
    assert response["error"]["code"] == "instance_in_use"
    a.require("release")
    clock.advance(125)
    service.tick()
    assert not platform.closed
    assert b.require("status", instance_id=first["instance"]["instance_id"])["use_count"] == 1


def test_pending_work_survives_lease_idle_and_release(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    result = client.ready(project)
    scope = client.require("scope_begin", lease_id=result["lease"]["lease_id"], operation="asset_compile")
    clock.advance(900)
    service.tick()
    assert client.require("release")["pending"]
    platform.busy = True
    assert not client.require("scope_end", scope_id=scope["scope_id"])["released"]
    assert not platform.closed
    platform.busy = False
    clock.advance(10)
    service.tick()
    assert client.require("status", instance_id=result["instance"]["instance_id"])["use_count"] == 0


def test_dirty_editor_reports_blocker_without_exit(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    result = client.ready(project)
    platform.blockers = ["dirty:/Game/M_Test"]
    client.require("release")
    client.require("close", instance_id=result["instance"]["instance_id"], dry_run=False)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        state = client.require("status", instance_id=result["instance"]["instance_id"])
        if state["state"] == "BLOCKED":
            break
        time.sleep(0.005)
    assert state["blockers"] == platform.blockers
    assert not platform.closed


def test_exclusive_scope_has_fifo_precedence(lifecycle):
    service, platform, clock, project = lifecycle
    clients = [Client(service) for _ in range(3)]
    leases = [client.ready(project)["lease"]["lease_id"] for client in clients]
    first = clients[0].require("scope_begin", lease_id=leases[0])
    writer = clients[1].require("scope_begin", lease_id=leases[1], exclusive=True)
    reader = clients[2].require("scope_begin", lease_id=leases[2])
    assert writer["waiting"] and reader["waiting"]
    clients[0].require("scope_end", scope_id=first["scope_id"])
    assert not clients[1].require("scope_begin", lease_id=leases[1], exclusive=True, scope_id=writer["scope_id"]).get("waiting")
    assert clients[2].require("scope_begin", lease_id=leases[2], scope_id=reader["scope_id"])["waiting"]
