"""Durable scopes, leases and uncertain process outcomes survive manager epochs."""

import os

from ue_node_nexus_mcp.instances.broker.service import BrokerService
from ue_node_nexus_mcp.instances.errors import InstanceError
from .conftest import Client


def test_active_and_queued_work_survive_a_manager_restart(lifecycle):
    service, platform, clock, project = lifecycle
    a, b = Client(service), Client(service)
    left, right = a.ready(project)["lease"]["lease_id"], b.ready(project)["lease"]["lease_id"]
    first = a.require("scope_begin", lease_id=left)
    second = b.require("scope_begin", lease_id=right, exclusive=True)
    replacement = BrokerService(service.root, platform, service.policy, clock)
    try:
        replacement.reconcile()
        assert replacement.epoch > service.epoch
        assert len(replacement.scopes.for_instance(first["instance_id"])) == 2
        a.service, b.service = replacement, replacement
        for client in (a, b):
            client.require("register", client_session_id=client.identifier, workspace="reconnected",
                           process_created=platform.inspect(os.getpid())["process_created"])
        a.require("scope_end", scope_id=first["scope_id"])
        assert not b.require("scope_begin", lease_id=right, scope_id=second["scope_id"], exclusive=True).get("waiting")
        b.require("scope_end", scope_id=second["scope_id"])
        assert not replacement.registry.all("scope")
        assert platform.spawn_count == 1
    finally:
        replacement.editors.workers.shutdown(wait=True)
        replacement.registry.close()


def test_lost_grant_response_is_durably_reconciled(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    lease = client.ready(project)["lease"]["lease_id"]
    original = platform.control

    def control(instance, action, payload):
        result = original(instance, action, payload)
        if action == "scope_grant":
            raise InstanceError("instance_unresponsive", "injected response loss after grant")
        return result

    platform.control = control
    result = client.call("scope_begin", lease_id=lease)
    assert result["error"]["code"] == "instance_unresponsive"
    assert service.registry.all("scope")[0]["releasing"]
    assert platform.grants
    service.scopes.sweep()
    assert not service.registry.all("scope") and not platform.grants


def test_unverifiable_client_is_preserved(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    client.ready(project)
    original = platform.alive

    def alive(identity):
        if identity["pid"] == os.getpid():
            raise PermissionError("injected process query denial")
        return original(identity)

    platform.alive = alive
    clock.advance(15)
    service.tick()
    assert len(service.sessions.leases) == 1
    assert client.identifier in service.sessions.clients
    assert not platform.closed


def test_startup_timeout_keeps_same_process_and_launch_intent(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    result = client.ready(project)
    identifier = result["instance"]["instance_id"]
    platform.records[identifier]["ready"] = False
    service.instances[identifier]["state"] = "STARTING"
    clock.advance(250)
    service.editors.refresh()
    assert client.require("status", instance_id=identifier)["state"] == "UNRESPONSIVE"
    assert client.call("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)["error"]["code"] == "instance_unresponsive"
    assert platform.spawn_count == 1


def test_shutdown_timeout_preserves_slot_until_actual_process_exit(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    identifier = client.ready(project)["instance"]["instance_id"]
    service.instances[identifier].update(state="STOPPING", stopping_since=clock())
    clock.advance(61)
    service.editors.refresh()
    state = client.require("status", instance_id=identifier)
    assert state["state"] == "UNRESPONSIVE" and state["exit_committed"]
    assert state["error"]["code"] == "shutdown_timeout"
    platform.records.pop(identifier)
    service.editors.refresh()
    assert client.require("status", instance_id=identifier)["state"] == "EXITED"
