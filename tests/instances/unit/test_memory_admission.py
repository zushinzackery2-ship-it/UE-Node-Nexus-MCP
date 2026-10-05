"""Memory admission holds by default; only an explicit start may proceed below it."""

import threading

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.instances.tools.dispatch import execute
from tests.instances.support.sdk import LocalBroker
from .conftest import Client, session_ready

GIB = 1024 ** 3
# 64 GiB of RAM puts the 15 % floor at 9.6 GiB, above the 6 GiB available.
FLOOR = int(64 * GIB * 0.15)


@pytest.fixture
def short(lifecycle, monkeypatch):
    """A manager whose machine lacks the memory a start requires, and its lifecycle events."""
    service, platform, clock, project = lifecycle
    platform.memory = lambda: dict(total=64 * GIB, available=6 * GIB)
    events = []
    original = service.event

    def event(action, **fields):
        events.append(dict(fields, event=action))
        original(action, **fields)

    monkeypatch.setattr(service, "event", event)
    return service, platform, clock, project, events


def start(client, project, **payload):
    return client.call("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False, **payload)


def test_a_start_short_of_memory_is_refused_and_names_the_override(short):
    service, platform, _, project, events = short
    refused = start(Client(service), project)
    assert refused["error"]["code"] == "capacity_exceeded"
    assert "allow_low_memory=true" in refused["error"]["message"]
    details = refused["error"]["details"]
    assert details["override"] == "allow_low_memory"
    assert details["available_bytes"] == 6 * GIB
    assert details["floor_bytes"] == details["required_bytes"] == FLOOR and details["estimated_project_bytes"] == 0
    assert platform.spawn_count == 0
    logged = [item for item in events if item["event"] == "launch_refused"]
    assert logged[-1]["dry_run"] is False and logged[-1]["error"]["details"]["override"] == "allow_low_memory"


def test_an_explicit_override_starts_below_the_requirement_and_records_it(short):
    service, platform, _, project, events = short
    created = start(Client(service), project, allow_low_memory=True)
    assert created["ok"], created
    identifier = created["data"]["instance"]["instance_id"]
    service.editors.jobs[identifier].result(timeout=5)
    admission = created["data"]["instance"]["admission"]
    assert admission == dict(available_bytes=6 * GIB, required_bytes=FLOOR, floor_bytes=FLOOR,
                             estimated_project_bytes=0, memory_override=True)
    assert platform.spawn_count == 1
    stored = [row for row in service.registry.all("instance") if row["instance_id"] == identifier]
    assert stored[0]["admission"] == admission
    reserved = [item for item in events if item["event"] == "launch_reserved"]
    assert reserved[-1]["instance_id"] == identifier and reserved[-1]["admission"] == admission


def test_an_override_with_enough_memory_records_that_none_was_needed(lifecycle):
    service, _, _, project = lifecycle
    created = start(Client(service), project, allow_low_memory=True)
    assert created["ok"], created
    assert created["data"]["instance"]["admission"]["memory_override"] is False


def test_the_override_never_releases_the_startup_slot(short):
    service, platform, _, project, _ = short
    entered, resume = threading.Event(), threading.Event()
    original = platform.launch

    def launch(item):
        entered.set()
        assert resume.wait(5)
        return original(item)

    platform.launch = launch
    other = project.with_name("Other.uproject")
    other.touch()
    assert start(Client(service), project, allow_low_memory=True)["ok"]
    try:
        assert entered.wait(3)
        refused = start(Client(service), other, allow_low_memory=True)
        assert refused["error"]["code"] == "capacity_exceeded"
        assert "launch slot" in refused["error"]["message"]
    finally:
        resume.set()


@pytest.mark.parametrize("value", ["true", 1])
def test_only_a_boolean_overrides(short, value):
    service, platform, _, project, _ = short
    rejected = start(Client(service), project, allow_low_memory=value)
    assert rejected["error"]["code"] == "invalid_request"
    assert rejected["error"]["details"]["field"] == "allow_low_memory"
    assert platform.spawn_count == 0


def test_a_preview_binds_the_override_into_its_proposal(short):
    service, _, _, project, _ = short
    client = Client(service)
    preview = client.require("ensure", project_path=str(project), mode="reuse_or_start", allow_low_memory=True)
    assert preview["dry_run"] and preview["action"] == "start"
    assert preview["admission"]["memory_override"] is True
    without = start(client, project, proposal_id=preview["proposal_id"])
    assert without["error"]["code"] == "capacity_exceeded"
    created = start(client, project, allow_low_memory=True, proposal_id=preview["proposal_id"])
    assert created["ok"] and created["data"]["action"] == "created", created


def test_the_override_covers_one_start_and_is_not_kept_for_automatic_restarts(short):
    service, platform, _, project, _ = short
    session = EditorSession(LocalBroker(service, project.parent), str(project), startup_wait=0)
    try:
        envelope = execute("bridge_instance_ensure", dict(mode="reuse_or_start", dry_run=False, allow_low_memory=True), session)
        assert envelope["ok"], envelope
        assert envelope["data"]["startup"]["admission"]["memory_override"] is True
        assert any("below its memory requirement" in warning for warning in envelope["warnings"]), envelope["warnings"]
        assert "allow_low_memory" not in session.options
        identifier = envelope["data"]["instance"]["instance_id"]
        service.editors.jobs[identifier].result(timeout=5)
        session_ready(session, service)
        # The editor exits while this session still uses it; the next call restarts the project on its own.
        platform.records.pop(identifier)
        service.editors.refresh()
        with pytest.raises(InstanceError) as failure:
            session.reserve("asset_get")
        assert failure.value.code == "capacity_exceeded"
        assert failure.value.details["override"] == "allow_low_memory"
        assert platform.spawn_count == 1
    finally:
        session.close()


def test_a_manager_that_predates_the_override_is_never_sent_it(short, monkeypatch):
    service, platform, _, project, _ = short
    broker = LocalBroker(service, project.parent)
    sent = []
    original = broker.send

    def send(action, payload=None):
        sent.append(action)
        result = original(action, payload)
        # An older manager registers clients without announcing optional fields.
        return dict((key, value) for key, value in result.items() if key != "features")

    monkeypatch.setattr(broker, "send", send)
    session = EditorSession(broker, str(project), startup_wait=0)
    try:
        with pytest.raises(InstanceError) as failure:
            session.ensure(dict(mode="reuse_or_start", dry_run=False, allow_low_memory=True))
        assert failure.value.code == "manager_outdated"
        assert failure.value.details["feature"] == "allow_low_memory"
        assert "ensure" not in sent and platform.spawn_count == 0
    finally:
        session.close()
