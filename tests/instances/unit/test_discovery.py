"""Early external startup must converge from an OS identity to one Guard UUID."""

from ue_node_nexus_mcp.instances.identity.paths import project_identity
from tests.instances.unit.conftest import Client


def observations(project):
    early = dict(project_identity(project), pid=100000001, process_created="123", executable="editor",
                 instance_id="external-100000001-123", ready=False, guard_protocol=0, control_available=False)
    verified = dict(early, instance_id="guard-uuid", ready=True, guard_protocol=1, control_available=True,
                    engine_dir="engine", rhi="d3d12")
    return early, verified


def test_external_startup_promotes_one_process_without_duplicate_candidates(lifecycle):
    service, platform, _clock, project = lifecycle
    early, verified = observations(project)
    platform.records[early["instance_id"]] = early
    service.reconcile()
    client = Client(service)
    rejected = client.call("ensure", project_path=str(project), dry_run=False)
    assert rejected["error"]["code"] == "instance_unverified"
    platform.records.clear()
    platform.records[verified["instance_id"]] = verified
    service.reconcile()
    acquired = client.require("ensure", project_path=str(project), dry_run=False)
    assert acquired["action"] == "reused"
    assert acquired["instance"]["instance_id"] == verified["instance_id"]
    assert acquired["instance"]["ownership"] == "external" and acquired["instance"]["protected"]
    listed = client.require("list", project_path=str(project))["instances"]
    assert [item["instance_id"] for item in listed] == [verified["instance_id"]]
    assert platform.spawn_count == 0
    assert service.registry.get("instance", early["instance_id"]) is None
    with service.lock:
        service.editors.ingest([early])
    assert len(client.require("list", project_path=str(project))["instances"]) == 1


def test_different_process_creation_is_not_merged(lifecycle):
    service, _platform, _clock, project = lifecycle
    early, verified = observations(project)
    with service.lock:
        service.editors.ingest([early, dict(verified, process_created="124")])
    listed = Client(service).require("list", project_path=str(project))["instances"]
    assert len(listed) == 2
