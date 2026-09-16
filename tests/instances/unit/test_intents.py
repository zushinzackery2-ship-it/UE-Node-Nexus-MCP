from .conftest import Client


def test_same_launch_key_has_one_lease_and_cannot_resurrect_completed_launch(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    request = dict(project_path=str(project), mode="reuse_or_start", idempotency_key="one-request", dry_run=False)
    first = client.require("ensure", **request)
    client.ready(project)
    again = client.require("ensure", **request)
    assert first["instance"]["instance_id"] == again["instance"]["instance_id"]
    assert first["lease"]["lease_id"] == again["lease"]["lease_id"]
    identifier = first["instance"]["instance_id"]
    platform.records.pop(identifier)
    service.editors.refresh()
    assert client.call("ensure", **request)["error"]["code"] == "stale_instance"
    assert platform.spawn_count == 1


def test_idempotency_key_parameter_conflict_is_explicit(lifecycle):
    _, _, _, project = lifecycle
    client = Client(lifecycle[0])
    request = dict(project_path=str(project), mode="reuse_or_start", idempotency_key="same", dry_run=False)
    client.require("ensure", **request)
    assert client.call("ensure", **dict(request, rhi="nullrhi"))["error"]["code"] == "idempotency_conflict"


def test_start_preview_is_stale_when_another_client_started_the_project(lifecycle):
    service, platform, _, project = lifecycle
    a, b = Client(service), Client(service)
    request = dict(project_path=str(project), mode="reuse_or_start")
    token = a.require("ensure", **request)["proposal_id"]
    b.ready(project)
    assert a.call("ensure", **request, dry_run=False, proposal_id=token)["error"]["code"] == "stale_plan"
    assert platform.spawn_count == 1


def test_failed_reuse_does_not_bind_a_new_repository(lifecycle):
    service, _, _, project = lifecycle
    client = Client(service)
    assert client.call("ensure", project_path=str(project), dry_run=False)["error"]["code"] == "instance_missing"
    assert not list((service.root / "Projects").glob("*.json"))
