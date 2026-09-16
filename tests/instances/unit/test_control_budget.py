from concurrent.futures import ThreadPoolExecutor
import threading
import time

from .conftest import Client


def test_slow_native_grants_leave_status_and_release_available(lifecycle):
    service, platform, _, project = lifecycle
    clients = [Client(service) for _ in range(3)]
    leases = [client.ready(project)["lease"]["lease_id"] for client in clients]
    entered, release = threading.Barrier(3), threading.Event()
    original = platform.control

    def slow(instance, action, payload):
        if action == "scope_grant":
            entered.wait(timeout=5)
            assert release.wait(5)
        return original(instance, action, payload)

    platform.control = slow
    with ThreadPoolExecutor(max_workers=2) as pool:
        pending = [pool.submit(clients[index].call, "scope_begin", lease_id=leases[index]) for index in range(2)]
        try:
            entered.wait(timeout=5)
            started = time.monotonic()
            assert clients[2].require("list")["instances"]
            assert clients[2].require("release")["released"]
            assert time.monotonic() - started < 0.05
            rejected = clients[2].call("scope_begin", lease_id=leases[2])
            assert rejected["error"]["code"] == "manager_busy"
            assert rejected["error"]["details"]["submitted"] is False
        finally:
            release.set()
        assert all(request.result()["ok"] for request in pending)


def test_recorded_project_memory_increases_future_start_reservation(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    identifier = client.ready(project)["instance"]["instance_id"]
    platform.records[identifier]["private_working_set_bytes"] = 8 * 1024 ** 3
    service.editors.refresh()
    client.require("release")
    platform.records.pop(identifier)
    service.editors.refresh()
    platform.memory = lambda: dict(total=32 * 1024 ** 3, available=10 * 1024 ** 3)
    rejected = client.call("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    assert rejected["error"]["code"] == "capacity_exceeded"
    assert rejected["error"]["details"]["estimated_project_bytes"] == 8 * 1024 ** 3
    assert platform.spawn_count == 1
