"""Report durable native transactions even when cancellation interrupts later work."""


def reconcile_result(store, identifier, result):
    job = store.record("sync_job", identifier)
    records = [row for row in store.records("apply") if row.get("job_id") == identifier]
    committed = [row for row in records if row["phase"] == "completed"]
    pending = [row for row in records if row["phase"] in ("applying", "ue_committed", "recovery_required", "result_rejected")]
    if not records and job["operation"] not in ("push", "continue", "recover"):
        return result
    rows = list(result.get("rows", []))
    reported = {row.get("apply_id") for row in rows}
    for record in committed:
        if record["id"] in reported:
            continue
        row = dict(asset=record["asset"], action="pushed", apply_id=record["id"], commit_id=record["published"],
                   caller_refresh=not record.get("consume", True))
        if record.get("safety_validation") is not None:
            row["safety_validation"] = record["safety_validation"]
        if record.get("request", {}).get("delete_asset") or record.get("request", {}).get("delete_scene"):
            row["deleted"] = True
        rows.append(row)
    completed = list(dict.fromkeys(row["asset"] for row in committed))
    refresh = dict(job.get("pending_refresh", {}))
    for record in committed:
        if not record.get("consume", True) and record.get("requested_at", 0) >= refresh.get(record["asset"], float("inf")):
            refresh.pop(record["asset"], None)
    settled = set(completed) | {row["asset"] for row in pending}
    settled.update(row["asset"] for row in rows if row.get("action") == "unchanged" and row["asset"] not in refresh)
    result.update(rows=rows, applied=len(committed), completed_assets=completed,
                  unapplied_assets=[asset for asset in dict.fromkeys([*job.get("planned_assets", []), *refresh]) if asset not in settled],
                  pending_refresh_assets=sorted(refresh),
                  applied_transactions=[dict(apply_id=row["id"], asset=row["asset"], phase=row["phase"],
                                             commit_id=row["published"]) for row in committed],
                  pending_transactions=[dict(apply_id=row["id"], asset=row["asset"], phase=row["phase"]) for row in pending])
    if result.get("status") in ("cancelled", "failed", "interrupted") and committed:
        result["status"] = "partial"
    if pending:
        result.update(status="partial" if committed else "failed", error_count=max(1, result.get("error_count", 0)))
        result.setdefault("error", dict(code="recovery_required", message="native transactions require receipt reconciliation",
                                        details=dict(pending_transactions=result["pending_transactions"])))
    return result
