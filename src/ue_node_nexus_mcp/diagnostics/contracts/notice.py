"""Give retained runtime evidence explicit status, identity, and a detail route."""

from datetime import datetime, timezone

from ...diagnostic_counting import coerce_error_count
from .samples import compact
from .verdict import runtime_verdict


def normalize(notice: dict | None, items: list | None = None, *, recorded: bool = False) -> dict:
    if not isinstance(notice, dict) or not notice.get("session_id"):
        return dict(status="unknown", available=False, scope="unobserved",
                    live_state=False, samples=[], runtime_observed=False)
    value = dict(notice)
    value.setdefault("available", True)
    value.setdefault("sampled_at", datetime.now(timezone.utc).isoformat())
    value["scope"] = "pie_session" if value.get("pie") else "runtime_session"
    if recorded:
        value["observation_origin"] = "recorded"
    value["live_state"] = bool(value.get("active")) and not recorded and value.get("observation_origin") != "recorded"
    value["runtime_observed"] = value.get("available") is True
    verdict = runtime_verdict(value)
    has_errors = coerce_error_count(value.get("error_count")) or coerce_error_count(value.get("editor_error_count"))
    value["status"] = "failed" if has_errors else "clean" if verdict["passed"] else "unknown"
    source = value.pop("items", [])
    source = [*(value.get("samples") or []), *source]
    if items:
        source.extend(item for item in items if isinstance(item, dict)
                      and item.get("session_id") in (None, value["session_id"]))
    unique = dict()
    for item in source:
        if isinstance(item, dict):
            key = item.get("id") or (item.get("source"), item.get("message"), item.get("node_guid"))
            unique[key] = item
    value["samples"] = compact(list(unique.values()))
    if "editor_samples" in value:
        value["editor_samples"] = compact(value["editor_samples"])
    value["next_read"] = dict(tool="ue_read", args=dict(target="diagnostics", format="detail",
        query=dict(session_id=value["session_id"], severity="error", include_assets=False, include_history=False)))
    return value
