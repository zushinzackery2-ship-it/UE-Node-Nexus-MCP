"""Issues 1, 13, 14, 15 and 20: every pending transaction has a way out.

A recovery protocol that only answers when the caller already knows the apply id,
only clears one record at a time, refuses records whose workspace is gone, or
reports a failure without the compiler messages that explain it, leaves the
caller with a blocked repository and no next call.
"""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.sync_project import SyncError

from tests.collaboration.test_recovery import (  # noqa: F401
    MATERIAL,
    SECOND,
    call,
    committed,
    edit,
    project,
)


def two_pending(project, monkeypatch):
    """One push that reaches the editor for both assets and then loses the process."""
    from ue_node_nexus_mcp.transcode.collaboration.apply import transactions

    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    edit(project, SECOND, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", all=True, message="A")
    lost = []

    def crash(workspace, record, *args, **options):
        lost.append(record["id"])
        raise OSError("process lost before the receipt was published")

    monkeypatch.setattr(transactions, "publish", crash)
    call(project, "push", stop_on_error=False)
    monkeypatch.undo()
    assert len(lost) == 2, lost
    return lost


def test_recover_without_an_apply_id_clears_every_pending_transaction(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    pending = two_pending(project, monkeypatch)

    preview = call(project, "recover", dry_run=True)
    assert preview["status"] == "candidate"
    assert sorted(row["apply_id"] for row in preview["pending"]) == sorted(pending)
    assert preview["recover"]["action"] == "recover"
    assert sorted(item["options"]["apply_id"] for item in preview["abandon"]) == sorted(pending)

    result = call(project, "recover")
    assert result["status"] == "recovered", result
    assert result["recovered"] == 2 and result["error_count"] == 0
    assert all(store.record("apply", identifier)["phase"] == "completed" for identifier in pending)


def test_a_record_whose_workspace_is_gone_is_still_recoverable(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()

    record = store.record("apply", apply_id)
    store.put_record("apply", apply_id, dict(record, workspace_id="a-workspace-that-was-closed"),
                     [record["candidate"]], record["generation"])

    recovered = call(project, "recover", apply_id=apply_id)
    assert recovered["phase"] == "completed"
    assert recovered["published"] == store.ref("refs/ue/published")


def test_abort_gives_up_on_a_transaction_the_editor_cannot_finish(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()

    preview = call(project, "abort", apply_id=apply_id, dry_run=True)
    assert preview["status"] == "candidate" and preview["phase"] == "ue_committed"
    assert store.record("apply", apply_id)["phase"] == "ue_committed"

    aborted = call(project, "abort", apply_id=apply_id)
    assert aborted["phase"] == "rejected" and aborted["reason"] == "abandoned by request"
    # Abandoning is idempotent and does not resurrect a terminal record.
    assert call(project, "abort", apply_id=apply_id)["phase"] == "rejected"
    assert call(project, "push", [MATERIAL])["status"] in ("published", "partial")


def test_an_unknown_apply_id_names_the_ones_that_do_exist(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    with pytest.raises(SyncError) as failure:
        call(project, "abort", apply_id="not-an-apply")
    assert failure.value.code == "apply_not_found"
    assert failure.value.details["pending"] == [apply_id]


def test_a_named_resolution_is_validated_rather_than_guessed(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    with pytest.raises(SyncError) as failure:
        call(project, "recover", apply_id=apply_id, resolution="undo")
    assert failure.value.code == "invalid_option"
    assert failure.value.details["allowed"] == ["abandon", "inspect", "restore"]


def test_a_preview_blocked_by_pending_work_returns_the_calls_that_clear_it(project, monkeypatch):  # noqa: F811
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    edit(project, SECOND, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", all=True, message="B")

    with pytest.raises(SyncError) as failure:
        call(project, "push", [SECOND], dry_run=True)
    assert failure.value.code == "recovery_required"
    details = failure.value.details
    assert [row["apply_id"] for row in details["pending"]] == [apply_id]
    assert details["recover"] == dict(action="recover", options=dict(dry_run=False))
    assert details["abandon"] == [dict(action="abort", options=dict(apply_id=apply_id, dry_run=False))]


def test_a_rolled_back_apply_stops_the_repository_believing_its_own_memory(project, monkeypatch):  # noqa: F811
    """Issue 13: memory evidence survives only while the apply that produced it does."""
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    remembered = store.record("memory", "observed")
    assert remembered and MATERIAL in remembered["entries"]

    call(project, "abort", apply_id=apply_id)
    after = store.record("memory", "observed")
    assert MATERIAL not in (after or dict(entries=dict()))["entries"]
