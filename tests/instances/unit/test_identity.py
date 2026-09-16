import os

import pytest

from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.processes import inspect_process, is_alive, user_identity


def test_project_aliases_and_same_name_distinct_projects(tmp_path):
    first, second = tmp_path / "one", tmp_path / "two"
    first.mkdir()
    second.mkdir()
    a, b = first / "Demo.uproject", second / "Demo.uproject"
    a.touch()
    b.touch()
    identity = project_identity(a)
    assert identity == project_identity(first / ".." / "one" / "Demo.uproject")
    assert identity["project_key"] != project_identity(b)["project_key"]
    if os.name == "nt":
        assert identity == project_identity(str(a).upper())
    replacement = first / "replacement"
    replacement.touch()
    replacement.replace(a)
    assert identity == project_identity(a)


def test_symlink_resolves_to_same_project(tmp_path):
    project = tmp_path / "Demo.uproject"
    project.touch()
    link = tmp_path / "Alias.uproject"
    try:
        link.symlink_to(project)
    except OSError as exc:
        pytest.skip(str(exc))
    assert project_identity(link) == project_identity(project)


def test_process_identity_rejects_pid_reuse():
    identity = inspect_process(os.getpid())
    assert is_alive(identity)
    assert not is_alive(dict(identity, process_created="1"))
    assert user_identity()
