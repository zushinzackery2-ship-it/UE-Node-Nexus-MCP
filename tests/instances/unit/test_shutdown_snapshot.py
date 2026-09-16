"""The close result must describe the last native inspection, including clean state."""

from tests.instances.unit.conftest import Client


def test_committed_close_replaces_old_dirty_and_failure_snapshot(lifecycle):
    service, platform, _clock, project = lifecycle
    client = Client(service)
    acquired = client.ready(project)
    identifier = acquired["instance"]["instance_id"]
    service.instances[identifier].update(dirty_packages=["/Game/PreviouslyDirty"], failed_packages=["/Game/PreviouslyDirty"],
                                          blockers=["instance_dirty", "save_failed"])
    original = platform.control

    def control(instance, action, payload):
        result = original(instance, action, payload)
        if action == "commit_close":
            result.update(editor=dict(dirty_packages=[], failed_packages=[], blockers=[]))
        return result

    platform.control = control
    client.require("close", instance_id=identifier, dry_run=False)
    service.editors.jobs[identifier].result(timeout=5)
    state = client.require("status", instance_id=identifier)
    assert state["state"] == "STOPPING"
    assert state["dirty_packages"] == state["failed_packages"] == state["blockers"] == []
