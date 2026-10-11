"""Repository bindings retain their logical mirror path across storage junctions."""

import json
import os
import sqlite3
import subprocess
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.repository.binding import resolve
from ue_node_nexus_mcp.transcode.collaboration.apply.observe import bind
from ue_node_nexus_mcp.transcode.collaboration.store import Store


def repository(tmp_path, recorded_project=None):
    project_file = tmp_path / "Project.uproject"
    project_file.write_text("{}", encoding="utf-8")
    project = project_identity(str(project_file))
    mirror = tmp_path / "Saved/Nexus/Content_Transcoded"
    logical = mirror / "Project/.nexus/collaboration"
    physical = tmp_path / "Relocated-Storage"
    physical.mkdir()
    logical.parent.mkdir(parents=True)
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(logical), str(physical)],
                       check=True, capture_output=True)
    else:
        logical.symlink_to(physical, target_is_directory=True)
    with sqlite3.connect(physical / "index.sqlite") as connection:
        connection.execute("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)")
        connection.executemany("INSERT INTO meta VALUES (?, ?)", [
            ("project_id", "junction-project"),
            ("project_file", str(recorded_project or project_file))])
    binding = project_file.parent / "Saved/Nexus/collaboration-binding.json"
    binding.write_text(json.dumps(dict(repository=str(logical), project_id="junction-project")),
                       encoding="utf-8")
    return project, mirror, logical


def test_relocated_repository_keeps_mirror_and_project_identity(tmp_path):
    project, mirror, logical = repository(tmp_path)
    result = resolve(project, tmp_path / "Runtime", requested=str(mirror), persist=True)
    assert result["repository"] == str(logical)
    assert result["mirror_root"] == str(mirror)
    assert result["mirror_project_name"] == "Project"
    assert result["collaboration_project_id"] == "junction-project"


def test_relocated_repository_still_rejects_another_project(tmp_path):
    other = tmp_path / "Other.uproject"
    other.write_text("{}", encoding="utf-8")
    project, _, _ = repository(tmp_path, recorded_project=other)
    with pytest.raises(InstanceError) as failure:
        resolve(project, tmp_path / "Runtime")
    assert failure.value.code == "repository_mismatch"


def test_store_binds_the_existing_logical_editor_repository(tmp_path):
    _, mirror, logical = repository(tmp_path)
    store = Store(logical)

    def bridge(operation, payload):
        assert operation == "transcode_root_set"
        assert payload["collaboration_root"] == str(logical)
        assert payload["root"] == str(mirror)
        return dict(ok=True, data=dict(collaboration_version=1, editor_epoch="junction-test"))

    try:
        result = bind(bridge, SimpleNamespace(root=mirror, bridge_available=True), store)
        assert result["editor_epoch"] == "junction-test"
    finally:
        store.db.discard()
