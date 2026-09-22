"""Independent ancestors, partial publication, renames and collector semantics."""

from __future__ import annotations

import time
from copy import deepcopy
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.merge.engine import merge_snapshots
from ue_node_nexus_mcp.transcode.collaboration.merge.resolutions import resolve_tree
from ue_node_nexus_mcp.transcode.collaboration.merge.trees import merge_trees
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.validation import validate
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.store.maintenance import collect, finish, integrity
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.transcode.fixtures import material_raw

from .fake_bridge import ProtocolUe

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Rim.M_Rim"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    first = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    other = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, first, other, Store(Path(first["files_root"]).parents[2])


def call(project, action, workspace=None, paths=None, **options):
    ue, env = project[0], project[1]
    if workspace:
        options["workspace_id"] = workspace["id"]
    return run_sync(ue, action, paths, dict(dry_run=False, **options), env=env)


def change(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def value(ue, asset, name):
    return next(row["value"] for row in ue.assets[asset]["props"] if row["name"] == name)


def resolve_all(project, workspace, report, choice="ours"):
    for item in report["conflicts"]:
        call(project, "resolve", workspace, merge_id=report["merge_id"], conflict_id=item["conflict_id"], choice=choice)
    return call(project, "continue", workspace, merge_id=report["merge_id"])


def test_conflicting_ancestors_are_resolved_once_and_then_reused(project):
    ue, env, first, second, store = project
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    left = call(project, "commit", first, all=True, message="A")["commit_id"]
    change(second, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.07)")
    right = call(project, "commit", second, all=True, message="B")["commit_id"]
    resolve_all(project, first, call(project, "merge", first, revision=right))
    resolve_all(project, second, call(project, "merge", second, revision=left))
    assert call(project, "push", first)["status"] == "published"

    report = call(project, "push", second)
    assert report["status"] == "conflict" and report["roles"] == dict(ours="ancestor", theirs="ancestor"), report
    assert sorted(report["conflicts"][0]["snapshots"][1:]) != []
    published = resolve_all(project, second, report)
    assert published["status"] == "published", published
    # The ancestor key is scoped to the assets the publication covered, so the
    # record is found through the store rather than by rebuilding the scope here.
    bases = store.records("virtual_base")
    assert len(bases) == 1 and sorted(bases[0]["inputs"]) == sorted([left, right])
    recorded = bases[0]
    assert value(ue, MATERIAL, "BlendMode") == "BLEND_Translucent"

    change(second, MATERIAL, "TwoSided = true", "TwoSided = false")
    call(project, "commit", second, all=True, message="B again")
    again = call(project, "push", second)
    assert again["status"] == "published", again
    assert store.record("virtual_base", recorded["id"])["generation"] == recorded["generation"]


def test_a_dry_run_reports_ancestor_conflicts_without_opening_a_session(project):
    ue, env, first, second, store = project
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    left = call(project, "commit", first, all=True, message="A")["commit_id"]
    change(second, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.07)")
    right = call(project, "commit", second, all=True, message="B")["commit_id"]
    resolve_all(project, first, call(project, "merge", first, revision=right))
    resolve_all(project, second, call(project, "merge", second, revision=left))
    call(project, "push", first)
    preview = run_sync(ue, "push", options=dict(workspace_id=second["id"]), env=env)
    assert preview["status"] == "base-conflict", preview
    assert sorted(preview["base_ancestors"]) == sorted([left, right])
    assert [item for item in store.records("session") if item["metadata"].get("base_pair")] == []


def test_partial_publication_lets_both_agents_keep_editing(project):
    ue, env, first, second, store = project
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    change(first, SECOND, "BLEND_Translucent", "BLEND_Opaque")
    call(project, "commit", first, all=True, message="A edits both")
    assert call(project, "push", first, paths=[MATERIAL])["applied"] == 1
    assert value(ue, SECOND, "BlendMode") == "BLEND_Translucent"

    change(second, SECOND, "Constant(R=0.000001)", "Constant(R=0.09)")
    call(project, "commit", second, all=True, message="B edits the second graph")
    assert call(project, "push", second, paths=[SECOND])["applied"] == 1

    change(first, MATERIAL, "Constant(R=0.04)", "Constant(R=0.05)")
    call(project, "commit", first, all=True, message="A continues")
    complete = call(project, "push", first)
    assert complete["status"] == "published", complete
    assert [row for row in complete["rows"] if row["asset"] == MATERIAL][0]["action"] == "pushed"
    # The second asset carries A's property edit and B's published graph value.
    assert value(ue, SECOND, "BlendMode") == "BLEND_Opaque"
    constant = next(node for node in ue.assets[SECOND]["graph"]["nodes"] if node["class_short"] == "Constant")
    assert next(item["value"] for item in constant["props"] if item["name"] == "R") == "0.09"
    assert value(ue, MATERIAL, "BlendMode") == "BLEND_Translucent"
    assert len([row for row in complete["rows"] if row["action"] == "pushed"]) == 2


def test_an_already_consumed_change_is_not_applied_twice(project):
    ue, env, first, second, store = project
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="A")
    assert call(project, "push", first, paths=[MATERIAL])["applied"] == 1
    applied = len(ue.applied)
    repeated = call(project, "push", first)
    assert repeated["applied"] == 0, repeated
    assert len(ue.applied) == applied
    assert all(row["action"] == "unchanged" for row in repeated["rows"])


def _graph_entity(snapshot, alias):
    graph = snapshot["semantic"]["sections"]["graph:"]
    return next(key for key, entity in graph["entities"].items() if entity["alias"] == alias)


def test_a_rename_forks_identity_and_keeps_every_reference_consistent(tmp_path):
    store = Store(tmp_path)
    history = History(store)
    base = from_raw(material_raw())
    asset = base["raw"]["asset_path"]
    renamed = deepcopy(base)
    target = _graph_entity(base, "constant")
    graph = renamed["semantic"]["sections"]["graph:"]
    entity = graph["entities"].pop(target)
    fresh = "renamed-" + target[:8]
    graph["entities"][fresh] = dict(entity, alias="eps")
    graph["links"] = dict((slot, dict(link, src=fresh if link["src"] == target else link["src"]))
                          for slot, link in graph["links"].items())
    edited = deepcopy(base)
    edited["semantic"]["sections"]["graph:"]["entities"][target]["args"]["R"] = dict(type="float", state="explicit", value="0.5")

    result = merge_snapshots(base, renamed, edited)
    conflict = next(item for item in result.conflicts if item["conflict_type"] == "modify-delete")
    assert conflict["field_path"] == ["sections", "graph:", "entities", target]

    identifiers = [store.objects.put("snapshot", item) for item in (base, renamed, edited)]
    tree, conflicts = merge_trees(history, *[history.tree(dict([(asset, item)])) for item in identifiers])
    decided = resolve_tree(history, tree, dict(conflicts[0], asset=asset), dict(choice="ours"))
    candidate = store.objects.data(history.entries(decided)[asset], "snapshot")
    survivors = candidate["semantic"]["sections"]["graph:"]
    assert fresh in survivors["entities"] and target not in survivors["entities"]
    assert any(link["src"] == fresh for link in survivors["links"].values())
    assert validate(candidate) == []

    kept = resolve_tree(history, tree, dict(conflicts[0], asset=asset), dict(choice="theirs"))
    both = store.objects.data(history.entries(kept)[asset], "snapshot")["semantic"]["sections"]["graph:"]
    assert both["entities"][target]["args"]["R"]["value"] == "0.5"
    assert both["entities"][fresh]["alias"] == "eps"


def test_a_closed_workspace_stops_protecting_objects_after_its_retention(tmp_path):
    store = Store(tmp_path)
    history = History(store)
    raw = material_raw()
    snapshot = store.objects.put("snapshot", from_raw(raw))
    root = history.create(history.tree(dict([(raw["asset_path"], snapshot)])), [], "import")
    store.move("refs/heads/main", root, None)
    workspace = checkout(store, root)
    private = store.objects.put("snapshot", dict(only="held by the workspace record"))
    state = dict(workspace.state)
    roots = [state["head"], state["index"], private]
    store.put_record("workspace", state["id"], state, roots, state["generation"])
    start = time.time()

    assert private not in collect(store, dry_run=True, now=start)["garbage"]
    closed = store.record("workspace", state["id"])
    store.put_record("workspace", state["id"], dict(closed, closed=True), roots, closed["generation"])
    assert private not in collect(store, dry_run=False, now=start + 89 * 86400)["garbage"]

    expired = collect(store, dry_run=False, now=start + 91 * 86400)
    assert expired["expired_records"].get("workspace") == 1
    assert store.record("workspace", state["id"]) is None
    assert private in collect(store, dry_run=False, now=start + 200 * 86400)["garbage"]
    assert not store.objects.path(private).exists()


def test_an_interrupted_sweep_is_reported_and_resumed(tmp_path):
    store = Store(tmp_path)
    lost = store.objects.put("snapshot", dict(value="unreferenced"))
    with store.db.connection(write=True) as connection:
        connection.execute("INSERT INTO tombstones(id, kind, removed) VALUES (?, 'object', ?)", (lost, time.time()))
        connection.execute("DELETE FROM objects WHERE id=?", (lost,))
    report = integrity(store)
    assert report["pending_removal"] == [lost]
    assert store.objects.path(lost).is_file()
    resumed = collect(store, dry_run=False)
    assert resumed["resumed"] == [lost]
    assert not store.objects.path(lost).exists()
    assert integrity(store)["pending_removal"] == []


def test_recreating_a_tombstoned_object_keeps_its_bytes(tmp_path):
    store = Store(tmp_path)
    payload = dict(value="recreated between sweep phases")
    identifier = store.objects.put("snapshot", payload)
    with store.db.connection(write=True) as connection:
        connection.execute("INSERT INTO tombstones(id, kind, removed) VALUES (?, 'object', ?)", (identifier, time.time()))
        connection.execute("DELETE FROM objects WHERE id=?", (identifier,))
    assert store.objects.put("snapshot", payload) == identifier
    assert finish(store) == []
    assert store.objects.data(identifier, "snapshot") == payload


def test_reachability_follows_records_refs_and_the_reflog_window(tmp_path):
    store = Store(tmp_path)
    history = History(store)
    tree = history.tree(dict())
    root = history.create(tree, [], "root")
    moved = history.create(tree, [root], "moved on")
    store.move("refs/heads/main", root, None, "A", "start")
    store.move("refs/heads/main", moved, root, "A", "advance")
    orphan = store.objects.put("snapshot", dict(value="nobody"))
    start = time.time()
    assert collect(store, dry_run=True, now=start)["garbage"] == []
    assert collect(store, dry_run=False, now=start)["garbage"] == []
    swept = collect(store, dry_run=False, now=start + 31 * 86400)
    assert swept["garbage"] == [orphan]
    assert history.commit(root) and history.commit(moved)
    later = collect(store, dry_run=False, now=start + 200 * 86400)
    assert root not in later["garbage"]
    assert store.ref("refs/heads/main") == moved


def test_retention_windows_are_validated(tmp_path):
    store = Store(tmp_path)
    for retention, grace in ((0, 30), (90, 29)):
        with pytest.raises(SyncError) as failure:
            collect(store, dry_run=True, retention_days=retention, grace_days=grace)
        assert failure.value.code == "invalid_retention"
