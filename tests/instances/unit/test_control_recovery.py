"""Successful observations clear the previous failed control probe."""

from tests.instances.unit.test_discovery import observations


def test_ready_guard_clears_transient_control_error(lifecycle):
    service, _platform, _clock, project = lifecycle
    _early, ready = observations(project)
    with service.lock:
        service.editors.ingest([ready])
        service.editors.ingest([dict(ready, ready=False, control_available=False,
                                    control_error=dict(code="control_timeout", message="previous probe"))])
        assert service.instances[ready["instance_id"]]["state"] == "UNRESPONSIVE"
        service.editors.ingest([ready])
        recovered = service.instances[ready["instance_id"]]
        assert recovered["state"] == "IDLE" and recovered["ready"]
        assert "control_error" not in recovered and "error" not in recovered
        assert "control_error" not in service.registry.get("instance", ready["instance_id"])
