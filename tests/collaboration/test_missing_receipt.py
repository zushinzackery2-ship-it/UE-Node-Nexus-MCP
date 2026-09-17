"""A missing native receipt must not block unrelated publication."""

from .test_recovery import SECOND, call, committed, edit, project


def test_a_missing_receipt_is_abandoned_before_an_unrelated_publication(project, monkeypatch):
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    ue.receipts.pop(apply_id)

    edit(project, SECOND, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", all=True, message="B")
    result = call(project, "push", [SECOND])

    assert result["status"] == "published", result
    abandoned = store.record("apply", apply_id)
    assert abandoned["phase"] == "rejected"
    assert abandoned["reason"] == "durable receipt is missing; transaction cannot be resumed"
    assert result["errors"] == {}
