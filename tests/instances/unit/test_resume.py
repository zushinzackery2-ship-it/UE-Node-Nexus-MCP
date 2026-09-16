"""Clock delays protect live work and trigger reconciliation before reclamation."""

from .conftest import Client


def test_long_scheduling_gap_keeps_live_lease_and_work(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    acquired = client.ready(project)
    scope = client.require("scope_begin", lease_id=acquired["lease"]["lease_id"])
    platform.busy = True
    clock.advance(3600)
    service.tick()
    state = client.require("status", instance_id=acquired["instance"]["instance_id"])
    assert state["use_count"] == state["scope_count"] == 1
    assert service.recovery_until > clock() and not platform.closed
    assert not client.require("scope_end", scope_id=scope["scope_id"])["released"]
    platform.busy = False
    service.scopes.sweep()
    assert client.require("status", instance_id=state["instance_id"])["scope_count"] == 0


def test_resume_restarts_an_idle_grace_period_before_closing(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    identifier = client.ready(project)["instance"]["instance_id"]
    client.require("release")
    clock.advance(3600)
    service.tick()
    state = client.require("status", instance_id=identifier)
    assert state["state"] == "IDLE" and state["idle_deadline"] > clock()
    assert not platform.closed
