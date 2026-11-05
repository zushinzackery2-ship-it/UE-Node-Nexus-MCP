"""Measured project mounts must be copied privately and reproduced by the worker."""

from copy import deepcopy
from pathlib import Path

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import environment, mounts, service, snapshot
from ue_node_nexus_mcp.safety.isolation.inputs import validate_operations
from tests.safety.test_isolation import project
from tests.safety.test_publication_evidence import environment as measured_environment


def measured(tmp_path):
    descriptor = project(tmp_path)
    content = descriptor.parent / "Tests/Content"
    content.mkdir(parents=True)
    (content / "Probe.uasset").write_bytes(b"runtime content")
    return descriptor, dict(project_content_mounts=dict(Game="Content", Tests="Tests/Content"))


def test_dynamic_mount_preview_and_complete_private_copy(tmp_path, monkeypatch, all_features):
    descriptor, evidence = measured(tmp_path)
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.engine.resolve_engine", lambda *_: tmp_path / "engine")
    baseline = dict(environment=dict(measured_environment(), **evidence))
    operation = dict(operation="asset_create", payload=dict(asset_path="/Tests/New", asset_kind="material"))
    preview = service.validate(str(descriptor), [operation], None, "d3d12", 30, True, baseline=baseline)
    assert preview["ok"]
    records = preview["data"]["plugin_mounts"]
    assert records["Tests"]["project_local"] and records["Tests"]["content"] == str(descriptor.parent / "Tests/Content")
    plan = snapshot.inventory(descriptor, content_directories=["Tests/Content"])
    copied = snapshot.copy_project(plan, tmp_path / "copy", candidates={"Tests/Content/New.uasset"})
    target = Path(copied["project"]).parent / "Tests/Content/Probe.uasset"
    assert target.read_bytes() == b"runtime content" and not target.is_symlink()
    target.write_bytes(b"worker only")
    assert snapshot.unchanged(copied)
    (descriptor.parent / "Tests/Content/New.uasset").write_bytes(b"source addition")
    assert not snapshot.unchanged(copied)


def test_runtime_registration_overrides_a_plugin_with_the_same_mount_name(tmp_path, monkeypatch, all_features):
    descriptor, evidence = measured(tmp_path)
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.engine.resolve_engine", lambda *_: tmp_path / "engine")
    monkeypatch.setattr(mounts, "plugin_mounts", lambda *_: dict(Tests=dict(content=str(tmp_path / "engine/Content"))))
    baseline = dict(environment=dict(measured_environment(), **evidence, content_mounts=["Tests"]))
    operation = dict(operation="asset_create", payload=dict(asset_path="/Tests/New", asset_kind="material"))
    inspected = []
    original = service.inventory

    def inventory(*args, **kwargs):
        inspected.extend(kwargs["content_directories"])
        return original(*args, **kwargs)

    monkeypatch.setattr(service, "inventory", inventory)
    assert service.validate(str(descriptor), [operation], None, "d3d12", 30, True, baseline=baseline)["ok"]
    assert inspected == ["Tests/Content"]


@pytest.mark.parametrize("relative", ["../External/Content", "D:/External/Content", "Saved/Content", ".", "Tests/../../Content"])
def test_measured_directory_cannot_escape_or_copy_runtime_state(tmp_path, relative):
    descriptor, _ = measured(tmp_path)
    with pytest.raises(InstanceError):
        mounts.project_mounts(descriptor, dict(project_content_mounts=dict(Tests=relative)))


def test_measured_mount_reparse_ancestor_is_rejected(tmp_path):
    descriptor, _ = measured(tmp_path)
    external = tmp_path / "outside"
    external.mkdir()
    linked = descriptor.parent / "Linked"
    linked.symlink_to(external, target_is_directory=True)
    with pytest.raises(InstanceError) as error:
        mounts.project_mounts(descriptor, dict(project_content_mounts=dict(Tests="Linked")))
    assert error.value.code == "isolation_link_rejected"


@pytest.mark.parametrize("actual", [{}, dict(Tests="Other/Content")])
def test_worker_missing_or_moved_registration_is_rejected(actual):
    source = dict(measured_environment(), project_content_mounts=dict(Tests="Tests/Content"))
    worker = dict(environment=dict(deepcopy(source), project_content_mounts=actual))
    with pytest.raises(InstanceError) as error:
        environment.verify_worker(source, worker)
    assert error.value.code == "isolation_mount_mismatch"
    assert error.value.details["mount"] == "Tests"


def test_empty_registered_directory_survives_copy(tmp_path):
    descriptor, _ = measured(tmp_path)
    empty = descriptor.parent / "Empty/Content"
    empty.mkdir(parents=True)
    copied = snapshot.copy_project(snapshot.inventory(descriptor, content_directories=["Empty/Content"]), tmp_path / "copy")
    assert (Path(copied["project"]).parent / "Empty/Content").is_dir()


def test_empty_nested_mount_remains_private_beside_linked_content(tmp_path):
    descriptor, _ = measured(tmp_path)
    empty = descriptor.parent / "Content/Empty"
    empty.mkdir(parents=True)
    sibling = descriptor.parent / "Content/Other/Shared.uasset"
    sibling.parent.mkdir(parents=True)
    sibling.write_bytes(b"unchanged source")
    plan = snapshot.inventory(descriptor, content_directories=["Content/Empty", "Tests/Content"])
    copied = snapshot.copy_project(plan, tmp_path / "copy", candidates={"Tests/Content/New.uasset"})
    target = Path(copied["project"]).parent
    assert (target / "Content/Empty").is_dir() and not (target / "Content").is_symlink()
    assert (target / "Content/Other").is_symlink()
    (target / "Content/Empty/Worker.uasset").write_bytes(b"worker only")
    assert not (empty / "Worker.uasset").exists()
    assert snapshot.unchanged(copied)


def test_unknown_mount_error_identifies_nested_field_and_path(all_features):
    operation = dict(operation="transcode_apply", payload=dict(asset_path="/Game/Probe", kind="material",
        plan=[dict(op="set_asset_prop", name="Parent", value="/Missing/M.M")]))
    with pytest.raises(InstanceError) as error:
        validate_operations([operation], {"Game"}, {"Game"})
    assert error.value.details["field_path"] == "operations[0].payload.plan[0].value"
    assert error.value.details["path"] == "/Missing/M.M"


def test_dynamic_mount_copies_subtrees_named_like_caches(tmp_path):
    descriptor, _ = measured(tmp_path)
    asset = descriptor.parent / "Tests/Content/Saved/Asset.uasset"
    asset.parent.mkdir()
    asset.write_bytes(b"content folder, not a runtime cache")
    plan = snapshot.inventory(descriptor, content_directories=["Tests/Content"])
    copied = snapshot.copy_project(plan, tmp_path / "copy", candidates={"Tests/Content/New.uasset"})
    assert (Path(copied["project"]).parent / "Tests/Content/Saved/Asset.uasset").read_bytes() == asset.read_bytes()


def test_reparse_inside_measured_content_is_rejected(tmp_path):
    descriptor, _ = measured(tmp_path)
    linked = descriptor.parent / "Tests/Content/External"
    external = tmp_path / "external"
    external.mkdir()
    linked.symlink_to(external, target_is_directory=True)
    with pytest.raises(InstanceError) as error:
        snapshot.inventory(descriptor, content_directories=["Tests/Content"])
    assert error.value.code == "isolation_link_rejected"
