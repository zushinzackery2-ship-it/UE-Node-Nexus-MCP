"""Admission and close races at the same service boundary used by real IPC."""

from concurrent.futures import ThreadPoolExecutor
import threading
import time

from .conftest import Client


def test_release_cannot_overtake_a_native_grant(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    lease = client.ready(project)["lease"]["lease_id"]
    entered, resume = threading.Event(), threading.Event()
    original = platform.control

    def control(instance, action, payload):
        if action == "scope_grant":
            entered.set()
            assert resume.wait(5)
        return original(instance, action, payload)

    platform.control = control
    with ThreadPoolExecutor(max_workers=2) as pool:
        grant = pool.submit(client.require, "scope_begin", lease_id=lease, scope_id="racing")
        assert entered.wait(3)
        release = pool.submit(client.require, "scope_end", scope_id="racing")
        started = time.monotonic()
        assert client.require("list")["instances"]
        assert time.monotonic() - started < 0.5
        assert not release.done()
        resume.set()
        assert grant.result()["scope_id"] == "racing"
        assert release.result()["released"]
    assert not platform.grants
    assert not service.registry.all("scope")


def test_pending_release_cannot_reopen_scope(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    lease = client.ready(project)["lease"]["lease_id"]
    scope = client.require("scope_begin", lease_id=lease)
    platform.busy = True
    assert not client.require("scope_end", scope_id=scope["scope_id"])["released"]
    result = client.call("scope_begin", lease_id=lease, scope_id=scope["scope_id"])
    assert result["error"]["code"] == "scope_releasing"
    platform.busy = False
    service.scopes.sweep()
    assert not service.registry.all("scope")


def test_queue_and_parent_preserve_owner_and_exclusive_mode(lifecycle):
    service, _, _, project = lifecycle
    a, b = Client(service), Client(service)
    left, right = a.ready(project)["lease"]["lease_id"], b.ready(project)["lease"]["lease_id"]
    parent = a.require("scope_begin", lease_id=left)
    assert a.call("scope_begin", lease_id=left, parent_scope_id=parent["scope_id"], exclusive=True)["error"]["code"] == "exclusive_scope_required"
    queued = b.require("scope_begin", lease_id=right, exclusive=True)
    assert queued["waiting"]
    assert a.call("scope_begin", lease_id=left, scope_id=queued["scope_id"], exclusive=True)["error"]["code"] == "stale_scope"
    assert len(service.registry.all("scope")) == 2
    b.require("scope_end", scope_id=queued["scope_id"])
    assert len(service.registry.all("scope")) == 1


def test_new_acquire_cancels_drain_before_lease_success(lifecycle):
    service, platform, _, project = lifecycle
    a, b = Client(service), Client(service)
    identifier = a.ready(project)["instance"]["instance_id"]
    a.require("release")
    entered, resume = threading.Event(), threading.Event()
    original = platform.control
    cancelled = []

    def control(instance, action, payload):
        if action == "prepare_close":
            entered.set()
            assert resume.wait(5)
        if action == "cancel_close":
            cancelled.append(payload["close_id"])
        return original(instance, action, payload)

    platform.control = control
    a.require("close", instance_id=identifier, dry_run=False)
    try:
        assert entered.wait(3)
        result = b.require("ensure", project_path=str(project), dry_run=False)
        assert result["instance"]["state"] == "READY" and cancelled
        assert result["lease"]["instance_id"] == identifier
    finally:
        resume.set()
    service.editors.jobs[identifier].result(timeout=5)
    assert not platform.closed


def test_preview_is_invalidated_by_other_user_or_pin(lifecycle):
    _, _, _, project = lifecycle
    a, b = Client(lifecycle[0]), Client(lifecycle[0])
    identifier = a.ready(project)["instance"]["instance_id"]
    preview = a.require("close", instance_id=identifier)
    b.ready(project)
    assert a.call("close", instance_id=identifier, dry_run=False, proposal_id=preview["proposal_id"])["error"]["code"] == "stale_plan"
    b.require("release")
    preview = a.require("close", instance_id=identifier)
    a.require("pin", instance_id=identifier, reason="test retention", seconds=60, dry_run=False)
    assert a.call("close", instance_id=identifier, dry_run=False, proposal_id=preview["proposal_id"])["error"]["code"] == "stale_plan"


def test_stopping_process_keeps_capacity_and_cannot_be_acquired(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    identifier = client.ready(project)["instance"]["instance_id"]
    client.require("release")
    entered, resume = threading.Event(), threading.Event()
    original = platform.control

    def control(instance, action, payload):
        if action == "commit_close":
            entered.set()
            assert resume.wait(5)
        return original(instance, action, payload)

    platform.control = control
    client.require("close", instance_id=identifier, dry_run=False)
    try:
        assert entered.wait(3)
        result = client.call("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
        assert result["error"]["code"] == "instance_stopping"
        assert platform.spawn_count == 1
    finally:
        resume.set()
