"""A process that exited between sweeps must not be handed to the next acquire."""

from .conftest import Client


def test_ensure_resamples_a_cached_dead_editor_before_reusing(lifecycle):
    service, platform, clock, project = lifecycle
    client = Client(service)
    first = client.ready(project)
    instance = service.instances[first["instance"]["instance_id"]]
    platform.records.pop(instance["instance_id"])
    result = client.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    assert result["instance"]["instance_id"] != instance["instance_id"]
    assert service.instances[instance["instance_id"]]["state"] == "EXITED"


def test_cached_discovery_cannot_reintroduce_the_confirmed_dead_process(lifecycle, monkeypatch):
    service, platform, clock, project = lifecycle
    client = Client(service)
    first = client.ready(project)
    identifier = first["instance"]["instance_id"]
    stale = dict(platform.records.pop(identifier))
    monkeypatch.setattr(platform, "discover", lambda key=None: [stale])
    result = client.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    assert result["instance"]["instance_id"] != identifier
