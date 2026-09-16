"""Project reservations share one capacity domain, including blocked editors."""

from concurrent.futures import ThreadPoolExecutor
import threading
import time

from .conftest import Client


def test_different_projects_race_for_the_last_editor_slot(lifecycle):
    service, platform, _, project = lifecycle
    clients = [Client(service) for _ in range(8)]
    original = clients[0].ready(project)["instance"]["instance_id"]
    projects = [project.with_name("Other" + str(index) + ".uproject") for index in range(len(clients))]
    for path in projects:
        path.touch()
    barrier = threading.Barrier(len(clients))

    def compete(index):
        barrier.wait(timeout=5)
        return clients[index].call("ensure", project_path=str(projects[index]), mode="reuse_or_start", dry_run=False)

    with ThreadPoolExecutor(max_workers=len(clients)) as pool:
        results = list(pool.map(compete, range(len(clients))))
    accepted = [result["data"] for result in results if result["ok"]]
    assert len(accepted) == 1, results
    assert all(result["ok"] or result["error"]["code"] in ("capacity_exceeded", "manager_busy") for result in results)
    for job in service.editors.jobs.values():
        job.result(timeout=5)
    assert platform.spawn_count == 2
    assert original in service.instances and not platform.closed


def test_dirty_occupants_keep_capacity_when_a_third_project_arrives(lifecycle):
    service, platform, clock, project = lifecycle
    clients = [Client(service) for _ in range(3)]
    other = project.with_name("Other.uproject")
    third = project.with_name("Third.uproject")
    other.touch()
    third.touch()
    identifiers = [clients[index].ready(path)["instance"]["instance_id"] for index, path in enumerate((project, other))]
    platform.blockers = ["instance_dirty"]
    for client in clients[:2]:
        client.require("release")
    clock.advance(130)
    rejected = clients[2].call("ensure", project_path=str(third), mode="reuse_or_start", dry_run=False)
    assert rejected["error"]["code"] == "capacity_exceeded", rejected
    deadline = time.monotonic() + 5
    while any(service.instances[identifier]["state"] != "BLOCKED" for identifier in identifiers) and time.monotonic() < deadline:
        time.sleep(0.005)
    assert all(service.instances[identifier]["state"] == "BLOCKED" for identifier in identifiers)
    assert platform.spawn_count == 2 and not platform.closed
    for index, identifier in enumerate(identifiers):
        state = clients[index].require("status", instance_id=identifier)
        assert state["blockers"] == ["instance_dirty"]
