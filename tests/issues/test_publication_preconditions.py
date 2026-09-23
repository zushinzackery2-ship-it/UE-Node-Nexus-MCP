"""Issue 25: a publication must not be rejected for a change it made itself.

Compiling a published asset regenerates its class and UE reinstances what other
packages built from it, without dirtying anything. Remembered package evidence
cannot see that, so every revision UE is asked to verify is measured, and
measured again after an earlier apply of the same publication. A real writer is
still refused, and the refusal names the asset that moved.
"""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.sync import run_sync

from tests.collaboration.test_divergence import (  # noqa: F401
    MATERIAL,
    SECOND,
    call,
    change,
    project,
)

TEXTURE = "/Game/T/T_Rock.T_Rock"


def recompiled(ue, asset: str, mark: str = "recompiled") -> None:
    """Change memory the way a compile does: the package stays saved and clean."""
    if ue.assets[asset]["kind"] == "stub":
        ue.assets[asset]["tags"] = list(ue.assets[asset].get("tags", [])) + [dict(name=mark.replace(" ", "_"), value="1")]
    else:
        ue.assets[asset]["props"] = list(ue.assets[asset]["props"]) + [dict(name="PreviewMesh", type="FString", value=mark, default="")]


def committed(project, *assets):
    ue, env, first, second, store = project
    # A saved package: the editor reports its hash, so its memory evidence is
    # carried from one observation to the next instead of being exported.
    ue.assets[TEXTURE]["saved_hash"] = "t-saved"
    run_sync(ue, "fetch", None, dict(workspace_id=first["id"], dry_run=False), env=env)
    for asset in assets:
        change(first, asset, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", first, all=True, message="edit")
    return first


def remembered(store) -> dict:
    record = store.record("memory", "observed")
    return record["entries"] if record else dict()


def test_a_dependency_that_moved_without_dirtying_is_measured_not_remembered(project):  # noqa: F811
    ue, env, first, second, store = project
    workspace = committed(project, MATERIAL)
    assert TEXTURE in remembered(store)
    recompiled(ue, TEXTURE)
    result = call(project, "push", workspace)
    assert result["status"] == "published", result
    assert [row["action"] for row in result["rows"] if row["action"] == "pushed"] == ["pushed"]


def test_a_later_unit_re_measures_what_an_earlier_apply_moved(project):  # noqa: F811
    ue, env, first, second, store = project
    workspace = committed(project, MATERIAL, SECOND)
    # The first apply compiles and reinstances the texture both materials read.
    ue.after_apply = lambda: recompiled(ue, TEXTURE, "reinstanced")
    result = call(project, "push", workspace)
    assert result["status"] == "published", result
    assert sorted(row["asset"] for row in result["rows"] if row["action"] == "pushed") == sorted([MATERIAL, SECOND])


def test_another_writer_on_a_dependency_is_refused_and_named(project):  # noqa: F811
    ue, env, first, second, store = project
    workspace = committed(project, MATERIAL)
    ue.before_apply = lambda: recompiled(ue, TEXTURE, "edited elsewhere")
    result = call(project, "push", workspace)
    assert result["status"] == "partial", result
    failed = next(row for row in result["rows"] if row["action"] == "failed")
    assert failed["code"] == "stale_target"
    stale = failed["details"]["stale"]
    assert stale["role"] == "dependency" and stale["asset_path"] == TEXTURE
    assert stale["expected_revision"] != stale["live_revision"]
    # Neither guarded asset keeps evidence the failure contradicted.
    entries = remembered(store)
    assert MATERIAL not in entries and TEXTURE not in entries, sorted(entries)
    again = call(project, "push", workspace)
    assert again["status"] == "published", again


def test_another_writer_on_the_target_is_refused_as_the_target(project):  # noqa: F811
    ue, env, first, second, store = project
    workspace = committed(project, MATERIAL)
    ue.before_apply = lambda: recompiled(ue, MATERIAL, "edited elsewhere")
    result = call(project, "push", workspace)
    failed = next(row for row in result["rows"] if row["action"] == "failed")
    assert failed["details"]["stale"]["role"] == "target"
    assert failed["details"]["stale"]["asset_path"] == MATERIAL


def test_a_preview_measures_the_same_preconditions(project):  # noqa: F811
    ue, env, first, second, store = project
    workspace = committed(project, MATERIAL)
    recompiled(ue, TEXTURE)
    preview = run_sync(ue, "push", None, dict(workspace_id=workspace["id"], dry_run=True), env=env)
    assert preview["status"] == "preview" and not preview["errors"], preview
    assert call(project, "push", workspace, proposal_id=preview["proposal_id"])["status"] == "published"
