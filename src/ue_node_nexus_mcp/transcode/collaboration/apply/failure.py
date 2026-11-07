"""Expose the producing failure; full checkpoints stay in the durable transaction."""


def describe(workspace, record, response):
    receipt = record.get("receipt") or {}
    body = receipt.get("response_data") or (receipt.get("response") or {}).get("data") or response.get("data") or {}
    failed = body.get("failed") or []
    cause = receipt.get("failure") or (failed[0] if failed else None)
    if not cause:
        cause = (receipt.get("response") or {}).get("error") or response.get("error")
    return dict(cause=cause, receipt=dict(apply_id=record["id"], phase=record["phase"]),
                receipt_path=str(workspace.store.root / "transactions" / record["id"] / "apply.json"))
