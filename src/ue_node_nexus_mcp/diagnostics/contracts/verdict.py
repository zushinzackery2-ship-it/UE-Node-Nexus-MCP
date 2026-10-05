"""Evaluate one completed PIE window without mixing query pages or history."""

from ...diagnostic_counting import coerce_error_count
from math import isfinite


def runtime_verdict(runtime: dict, *, session_id: str | None = None,
                    instance_id: str | None = None, functional_passed: bool | None = None) -> dict:
    observed_session = runtime.get("session_id")
    identity_matches = bool(observed_session) and (session_id is None or observed_session == session_id)
    if instance_id is not None:
        identity_matches = identity_matches and runtime.get("instance_id") == instance_id
    if not identity_matches:
        return dict(status="inconclusive", passed=False, code="runtime_session_mismatch",
                    session_id=observed_session, expected_session_id=session_id)
    errors = coerce_error_count(runtime.get("error_count"))
    if errors:
        return dict(status="failed", passed=False, code="runtime_diagnostics_failed",
                    session_id=observed_session, error_count=errors)
    if functional_passed is False:
        return dict(status="failed", passed=False, code="functional_checks_failed",
                    session_id=observed_session, error_count=0)
    count = runtime.get("error_count")
    known_count = isinstance(count, (int, float)) and not isinstance(count, bool) and isfinite(count) and count >= 0
    complete = (known_count and runtime.get("available") is True and runtime.get("sources_complete") is True and runtime.get("pie") is True
                and runtime.get("active") is False and runtime.get("asset_counts_complete") is True
                and runtime.get("dropped_count") == 0
                and not runtime.get("cursor_gap"))
    if not complete:
        return dict(status="inconclusive", passed=False, code="runtime_observation_incomplete",
                    session_id=observed_session, error_count=0)
    return dict(status="passed", passed=True, code="runtime_diagnostics_clean",
                session_id=observed_session, error_count=0)
