"""Issues 18, 21 and 22: conflicts a caller can actually act on.

A publication that covers one asset must not inherit every historical validation
finding the project ever accumulated, a preview that cannot open a session must
say which call does, and one decision must be able to settle the whole batch it
applies to.
"""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.merge.trees import pair_key
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from tests.collaboration.test_divergence import (  # noqa: F401
    MATERIAL,
    SECOND,
    call,
    change,
    project,
)


def diverged(project, *assets):
    """Two workspaces that changed the same values, so a push has to merge."""
    ue, env, first, second, store = project
    for asset in assets or (MATERIAL,):
        change(first, asset, "Constant(R=0.000001)", "Constant(R=0.04)")
        change(second, asset, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", first, all=True, message="A")
    call(project, "commit", second, all=True, message="B")
    assert call(project, "push", first)["status"] == "published"
    return first, second


def test_a_preview_that_would_conflict_names_the_call_that_opens_the_session(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project)

    from ue_node_nexus_mcp.transcode.sync import run_sync

    preview = run_sync(ue, "push", None, dict(workspace_id=loser["id"], dry_run=True), env=env)
    assert preview["status"] == "preview" and preview["dry_run"] is True and preview["applied"] == 0
    assert preview["conflict_count"] > 0, preview
    # A preview runs against a throwaway copy and cannot open a durable session,
    # so it names the call that does instead of returning merge_id=None alone.
    assert preview["merge_id"] is None
    assert preview["resolve_with"]["action"] == "push"
    assert preview["resolve_with"]["options"]["dry_run"] is False
    assert "merge_id" in preview["resolve_with"]["note"]


def test_one_decision_settles_every_conflict_it_matches(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project)
    report = call(project, "push", loser)
    assert report["status"] == "conflict" and report["conflict_count"] >= 1

    result = call(project, "resolve", loser, merge_id=report["merge_id"], all=True, choice="ours")
    assert result["resolved_count"] == report["conflict_count"]
    assert sorted(result["resolved"]) == sorted(item["conflict_id"] for item in report["conflicts"])
    assert result["status"] == "ready"
    assert call(project, "continue", loser, merge_id=report["merge_id"])["status"] == "published"


def test_a_decision_can_be_scoped_to_one_asset_and_leave_the_rest_open(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project, MATERIAL, SECOND)
    report = call(project, "push", loser)
    assert sorted(set(item["asset"] for item in report["conflicts"])) == sorted([MATERIAL, SECOND])
    open_on_second = [item["conflict_id"] for item in report["conflicts"] if item["asset"] == SECOND]

    by_asset = call(project, "resolve", loser, merge_id=report["merge_id"], asset=MATERIAL, choice="ours")
    assert by_asset["status"] == "conflict"
    assert all(item["asset"] == SECOND for item in by_asset["conflicts"])
    assert not set(by_asset["resolved"]) & set(open_on_second)

    rest = call(project, "resolve", loser, merge_id=report["merge_id"], conflict_ids=open_on_second, choice="ours")
    assert rest["status"] == "ready" and sorted(rest["resolved"]) == sorted(open_on_second)


def test_a_wildcard_asset_filter_matches_a_package_prefix(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project, MATERIAL, SECOND)
    report = call(project, "push", loser)
    result = call(project, "resolve", loser, merge_id=report["merge_id"], asset="/Game/Materials/*", choice="ours")
    assert result["resolved_count"] == report["conflict_count"] and result["status"] == "ready"


def test_a_selection_that_names_nothing_is_refused_with_what_is_available(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project)
    report = call(project, "push", loser)

    with pytest.raises(SyncError) as failure:
        call(project, "resolve", loser, merge_id=report["merge_id"], choice="ours")
    assert failure.value.code == "invalid_request"
    assert failure.value.details["conflict_count"] == report["conflict_count"]
    assert failure.value.details["conflict_types"]
    assert failure.value.details["layers"]

    with pytest.raises(SyncError) as unknown:
        call(project, "resolve", loser, merge_id=report["merge_id"], conflict_ids=["nope"], choice="ours")
    assert unknown.value.code == "conflict_not_found" and unknown.value.details["conflict_ids"] == ["nope"]

    with pytest.raises(SyncError) as empty:
        call(project, "resolve", loser, merge_id=report["merge_id"], asset="/Game/Nothing.Nothing", choice="ours")
    assert empty.value.code == "conflict_not_found"


def test_a_batch_refuses_the_two_choices_that_name_one_conflict(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project, MATERIAL, SECOND)
    report = call(project, "push", loser)
    assert report["conflict_count"] >= 2
    with pytest.raises(SyncError) as failure:
        call(project, "resolve", loser, merge_id=report["merge_id"], all=True, choice="custom", value="1")
    assert failure.value.code == "invalid_resolution"


def test_conflict_ids_must_be_a_list_of_names(project):  # noqa: F811
    ue, env, first, second, store = project
    _, loser = diverged(project)
    report = call(project, "push", loser)
    with pytest.raises(SyncError) as failure:
        call(project, "resolve", loser, merge_id=report["merge_id"], conflict_ids="one", choice="ours")
    assert failure.value.code == "invalid_option"


def test_a_narrowed_ancestor_is_never_reused_as_a_project_wide_one():
    """Issue 18: a base resolved for one asset does not answer for the project."""
    wide = pair_key("left", "right")
    narrow = pair_key("left", "right", [MATERIAL])
    assert wide != narrow
    assert pair_key("left", "right", [MATERIAL]) == pair_key("left", "right", [MATERIAL])
    assert pair_key("left", "right", [MATERIAL, SECOND]) != narrow
    # Order of the pair and of the scope is not part of the identity.
    assert pair_key("right", "left", [SECOND, MATERIAL]) == pair_key("left", "right", [MATERIAL, SECOND])
