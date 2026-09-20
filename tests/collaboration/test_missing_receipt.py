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


def test_preflight_evidence_does_not_mask_stale_target_or_leave_a_pending_apply(project):
    ue, env, workspace, store = project
    edit(project, SECOND, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", all=True, message="B")
    ue.before_apply = lambda: ue.assets[SECOND]["props"].append(dict(name="TwoSided", type="bool", value="True", default="False"))

    def preflight_evidence(operation, payload, **kwargs):
        response = ue.call(operation, payload, **kwargs)
        if operation == "transcode_apply" and (response.get("error") or {}).get("code") == "stale_target":
            response["data"]["receipt"] = dict(current=ue.stamped(ue.assets[SECOND]))
        return response

    result = call(project, "push", [SECOND], bridge=preflight_evidence)
    assert result["errors"][SECOND]["code"] == "stale_target", result
    record = next(item for item in store.records("apply") if item["asset"] == SECOND)
    assert record["phase"] == "rejected"
    assert record["receipt"] is None
    assert ue.applied == []
