"""Bound the lifetime of catalog readiness evidence."""

import time


def fields(instance, seen, policy):
    if instance["state"] == "EXITED":
        return dict(snapshot_age_seconds=None, stale=True, live_state=False)
    if seen is None:
        return dict(snapshot_age_seconds=None, stale=True, ready=False)
    age = max(0.0, time.time() - seen)
    limit = max(policy.suspect_seconds, policy.sweep_seconds * 3)
    if age <= limit:
        return dict(snapshot_age_seconds=round(age, 3), stale=False)
    return dict(snapshot_age_seconds=round(age, 3), stale=True, ready=False,
                error=instance.get("error") or dict(code="snapshot_stale", message="no lifecycle observation within the freshness window"))
