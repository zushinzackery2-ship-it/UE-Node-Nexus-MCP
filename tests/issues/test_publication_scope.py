"""Issues 18, 19, 23 and 24: what a push merges, checks and reports.

A preview and the session a real push opens are one computation. A state the
push did not create is history and is not re-validated. A schema rebuild re-reads
older states instead of stranding the workspace. A conflict names large values
rather than carrying them, and still resolves to them.
"""

from __future__ import annotations

from pathlib import Path

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.merge.engine import INLINE_VALUE_BYTES, shown
from ue_node_nexus_mcp.transcode.collaboration.merge.resolutions import resolve_tree
from ue_node_nexus_mcp.transcode.collaboration.merge.trees import merge_trees
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.validation import validate
from ue_node_nexus_mcp.transcode.schema.lock import find_any_schema_lock
from ue_node_nexus_mcp.transcode.sync import run_sync

from tests.collaboration.helpers import ASSET, repository
from tests.collaboration.test_divergence import (  # noqa: F401
    MATERIAL,
    SECOND,
    call,
    change,
    project,
)
from tests.issues.test_conflict_resolution import diverged

RELOADED = "5.5.4-reloaded"


def preview(project, workspace):
    ue, env = project[0], project[1]
    return run_sync(ue, "push", None, dict(workspace_id=workspace["id"], dry_run=True), env=env)


def retire(project, asset):
    """UE saves ``asset`` with a node class the current catalog no longer has."""
    ue, env, first, second, store = project
    node = next(item for item in ue.assets[asset]["graph"]["nodes"] if item["class_short"] == "Constant")
    node.update(class_short="RetiredExpression", **{"class": "/Script/Engine.MaterialExpressionRetiredExpression"})
    ue.assets[asset]["saved_hash"] = "saved-elsewhere"
    run_sync(ue, "fetch", None, dict(workspace_id=first["id"], dry_run=False), env=env)
    observed = History(store).entries(store.ref("refs/ue/observed"))[asset]
    assert validate(store.objects.data(observed, "snapshot"), find_any_schema_lock(Path(env["UE_NEXUS_TRANSCODE_DIR"])))


def test_the_preview_reports_the_conflicts_the_session_opens_with(project):  # noqa: F811
    _, loser = diverged(project)
    # A full workspace selects every asset. The unrelated one UE saved in a state
    # the catalog rejects is history: neither the preview nor the session it
    # opens re-validates it, so the two agree and name only the real conflict.
    retire(project, SECOND)
    previewed = preview(project, loser)
    opened = call(project, "push", loser)
    assert opened["status"] == "conflict"
    assert previewed["conflict_count"] == opened["conflict_count"] > 0
    assert sorted(item["conflict_id"] for item in previewed["conflicts"]) == sorted(item["conflict_id"] for item in opened["conflicts"])
    assert set(item["asset"] for item in opened["conflicts"]) == set([MATERIAL])


def test_a_state_the_push_did_not_create_is_not_validated_again(project):  # noqa: F811
    ue, env, first, second, store = project
    retire(project, SECOND)
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="one asset")
    assert preview(project, first)["conflict_count"] == 0
    result = call(project, "push", first)
    assert result["status"] == "published", result
    assert result["conflicts"] == []


def test_a_rebuilt_schema_re_reads_committed_work_instead_of_stranding_it(project):  # noqa: F811
    ue, env, first, second, store = project
    change(first, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="before the rebuild")
    ue.schema_key = RELOADED
    run_sync(ue, "schema", None, dict(refresh=True), env=env)
    looked = preview(project, first)
    assert looked["conflict_count"] == 0, looked["conflicts"]
    result = call(project, "push", first)
    assert result["status"] == "published", result


def test_a_session_opened_after_a_rebuild_is_not_stale_on_arrival(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project)
    ue.schema_key = RELOADED
    run_sync(ue, "schema", None, dict(refresh=True), env=env)
    report = call(project, "push", loser)
    assert report["status"] == "conflict"
    assert all(item["conflict_type"] != "history_schema_conflict" for item in report["conflicts"]), report["conflicts"]
    settled = call(project, "resolve", loser, merge_id=report["merge_id"], all=True, choice="ours")
    assert settled["status"] == "ready"
    assert call(project, "continue", loser, merge_id=report["merge_id"])["status"] == "published"


def text_state(store, value: str) -> str:
    raw = dict(raw_version=1, asset_path=ASSET, class_short="DataAsset", kind="asset", schema_key="test-schema",
               props=[dict(name="Notes", type="FString", value=value, default="")])
    raw["class"] = "/Script/Engine.DataAsset"
    return store.objects.put("snapshot", from_raw(raw))


def test_a_large_value_is_named_by_the_conflict_and_still_resolves(tmp_path):
    store, _ = repository(tmp_path)
    history = History(store)
    base, ours, theirs = ("x" * 10, "o" * (INLINE_VALUE_BYTES * 2), "t" * (INLINE_VALUE_BYTES * 2))
    trees = [history.tree({ASSET: text_state(store, value)}) for value in (base, ours, theirs)]
    candidate, conflicts = merge_trees(history, *trees)
    assert len(conflicts) == 1
    conflict = conflicts[0]
    assert conflict["ours"]["state"] == "elided" and conflict["theirs"]["state"] == "elided"
    assert conflict["ours"]["bytes"] > INLINE_VALUE_BYTES
    assert conflict["base"] != dict(state="elided") and conflict["base"]["value"] == base
    for choice, expected in (("theirs", theirs), ("ours", ours), ("base", base)):
        resolved = resolve_tree(history, candidate, conflict, dict(choice=choice))
        snapshot = store.objects.data(history.entries(resolved)[ASSET], "snapshot")
        props = next(section for section in snapshot["semantic"]["sections"].values() if section["props"])["props"]
        assert props["Notes"]["value"] == expected, choice


def test_a_small_value_is_carried_inline():
    assert shown(dict(value="short")) == dict(value="short")
    elided = shown(dict(value="y" * (INLINE_VALUE_BYTES + 1)))
    assert elided["state"] == "elided" and len(elided["digest"]) == 24
