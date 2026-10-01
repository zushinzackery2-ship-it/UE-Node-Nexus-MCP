"""Admission follows startup pressure and memory, regardless of editor count."""

import json
import threading

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle.policy import Policy
from .conftest import Client


def test_six_interactive_projects_remain_available(lifecycle):
    service, platform, _, project = lifecycle
    client = Client(service)
    identifiers = []
    for index in range(6):
        path = project.with_name(f"Project{index}.uproject")
        path.touch()
        result = client.require("ensure", project_path=str(path), mode="reuse_or_start", launch_profile="interactive", dry_run=False)
        service.editors.jobs[result["instance"]["instance_id"]].result(timeout=5)
        service.reconcile()
        identifiers.append(result["instance"]["instance_id"])
    assert platform.spawn_count == 6
    assert len(set(identifiers)) == 6
    assert all(service.instances[key]["protected"] for key in identifiers)
    assert "max_editors" not in client.require("list")["policy"]


def test_dirty_occupants_do_not_prevent_another_project(lifecycle):
    service, platform, _, project = lifecycle
    clients = [Client(service) for _ in range(3)]
    identifiers = []
    for index in range(2):
        path = project.with_name(f"Dirty{index}.uproject")
        path.touch()
        identifiers.append(clients[index].ready(path)["instance"]["instance_id"])
    platform.blockers = ["instance_dirty"]
    for index, identifier in enumerate(identifiers):
        clients[index].require("close", instance_id=identifier, dry_run=False)
        service.editors.jobs[identifier].result(timeout=5)
    third = clients[2].ready(project)
    assert third["instance"]["instance_id"] not in identifiers
    assert platform.spawn_count == 3 and not platform.closed
    assert all(service.instances[key].get("blockers") == ["instance_dirty"] for key in identifiers)


def test_a_start_in_progress_still_holds_the_launch_slot(lifecycle):
    service, platform, _, project = lifecycle
    entered, resume = threading.Event(), threading.Event()
    original = platform.launch

    def launch(item):
        entered.set()
        assert resume.wait(5)
        return original(item)

    platform.launch = launch
    first, second = Client(service), Client(service)
    other = project.with_name("Other.uproject")
    other.touch()
    first.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    try:
        assert entered.wait(3)
        result = second.call("ensure", project_path=str(other), mode="reuse_or_start", dry_run=False)
        assert result["error"]["code"] == "capacity_exceeded"
        assert "launch slot" in result["error"]["message"]
    finally:
        resume.set()


def test_retired_editor_limit_is_migrated_and_other_options_still_validate(tmp_path):
    path = tmp_path / "policy.json"
    path.write_text(json.dumps(dict(max_editors=2, max_startups=3)), encoding="utf-8")
    policy = Policy.read(tmp_path)
    assert policy.max_startups == 3
    assert not hasattr(policy, "max_editors")
    assert json.loads(path.read_text(encoding="utf-8")) == dict(max_startups=3)
    path.write_text(json.dumps(dict(max_edtiors=2)), encoding="utf-8")
    with pytest.raises(InstanceError, match="unknown policy options"):
        Policy.read(tmp_path)
