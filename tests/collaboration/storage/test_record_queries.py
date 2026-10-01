"""History payload size must not multiply the cost of workspace observers."""

import json
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.service import run, ensure_idle
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from tests.transcode.fixtures import material_raw


@pytest.fixture
def history(tmp_path):
    project = tmp_path / "project"
    store = Store(project / ".nexus/collaboration")
    history = History(store)
    state = from_raw(material_raw())
    identifier = store.objects.put("snapshot", state)
    tree = history.tree(dict([(state["raw"]["asset_path"], identifier)]))
    commit = history.create(tree, [], "import")
    workspaces = [checkout(store, commit) for _ in range(8)]
    for index, workspace in enumerate(workspaces):
        for category, field, terminal in (("session", "status", "completed"), ("apply", "phase", "completed")):
            record = dict(workspace_id=workspace.state["id"], padding="history" * 150000, operation="push")
            record[field] = terminal
            store.put_record(category, f"{category}-{index}", record)
    yield store, workspaces, SimpleNamespace(project=project, project_file="", schema=None)
    store.db.discard()


def decoded_bytes(monkeypatch):
    original = json.loads
    sizes = []

    def load(payload, *args, **kwargs):
        if isinstance(payload, str) and '"padding"' in payload:
            sizes.append(len(payload))
        return original(payload, *args, **kwargs)

    monkeypatch.setattr(json, "loads", load)
    return sizes


def test_status_and_idle_guards_skip_completed_history(history, monkeypatch):
    store, workspaces, _ = history
    sizes = decoded_bytes(monkeypatch)
    workspace = workspaces[0]
    assert workspace.status()["sessions"] == []
    ensure_idle(workspace, "commit")
    assert sizes == [], f"decoded {sum(sizes)} bytes of terminal history"


def test_workspaces_are_metadata_only_without_worktree_scans(history, monkeypatch):
    store, workspaces, context = history
    sizes = decoded_bytes(monkeypatch)
    result = run(None, context, "workspaces", None, dict())
    assert len(result["workspaces"]) == len(workspaces)
    assert sizes == [], f"decoded {sum(sizes)} bytes while enumerating workspaces"
    assert all(row["status"]["detail"] == "summary" for row in result["workspaces"])
    assert all("unstaged" not in row["status"] for row in result["workspaces"])


def test_active_record_summaries_are_small_and_scoped(history, monkeypatch):
    store, workspaces, _ = history
    owner = workspaces[0].state["id"]
    for workspace in workspaces[:2]:
        store.put_record("session", workspace.state["id"], dict(workspace_id=workspace.state["id"],
            status="conflict", operation="push", conflicts=[dict(conflict_id="one")], padding="x" * 1000000))
    sizes = decoded_bytes(monkeypatch)
    rows = store.records("session", workspace_id=owner, exclude_status=("completed", "aborted"), summary=True)
    assert [row["id"] for row in rows] == [owner]
    assert rows[0]["conflict_count"] == 1 and "padding" not in rows[0]
    assert sizes == []
    assert len(store.record("session", owner)["padding"]) == 1000000


def test_legacy_records_get_indexed_once_and_remain_generation_checked(history):
    store, workspaces, context = history
    owner = workspaces[0].state["id"]
    store.put_record("session", "pending", dict(workspace_id=owner, status="conflict", conflicts=[], padding="x" * 10000))
    with store.db.connection(write=True) as connection:
        connection.execute("DROP TABLE IF EXISTS record_metadata")
        connection.execute("PRAGMA user_version=1")
    store.db.discard()
    upgraded = Store(store.root)
    rows = upgraded.records("session", workspace_id=owner, exclude_status=("completed",), summary=True)
    assert len(rows) == 1 and rows[0]["id"] == "pending"
    generation = rows[0]["generation"]
    upgraded.put_record("session", "pending", dict(workspace_id=owner, status="completed"), expected=generation)
    assert upgraded.records("session", workspace_id=owner, exclude_status=("completed",), summary=True) == []
    upgraded.drop_record("session", "pending", expected=generation + 1)
    upgraded.db.discard()
