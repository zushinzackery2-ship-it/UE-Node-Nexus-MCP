"""Preserve observed context through sync reports and durable job records."""

from .notice import normalize
from .response import attach, DIAGNOSTIC_FIELDS


def successful(report: dict) -> bool:
    return not (report.get("error_count") or report.get("conflict_count") or report.get("stopped")) and report.get("status") not in (
        "failed", "cancelled", "interrupted", "conflict", "base-conflict", "stale", "recovery_required", "blocked", "partial")


def attach_report(report: dict, action: str, observed: dict | None = None, *, recorded: bool = False) -> dict:
    source = dict(ok=successful(report), data=report)
    notice = report.get("runtime_diagnostics") or observed
    if isinstance(notice, dict):
        source["runtime_diagnostics"] = notice
    if isinstance(report.get("operation_diagnostics"), dict):
        source["operation_diagnostics"] = report["operation_diagnostics"]
    attach(report, source, "ue_sync_" + action, recorded=recorded)
    return report


def stored_fields(report: dict) -> dict:
    return dict((key, report[key]) for key in DIAGNOSTIC_FIELDS if key in report)


def retain_record(record: dict, response: dict, *, origin: str = "apply_response") -> None:
    notice = response.get("runtime_diagnostics")
    if isinstance(notice, dict) and notice.get("session_id"):
        value = normalize(notice, recorded=True)
        value["evidence_origin"] = origin
        record.update(runtime_diagnostics=value, runtime_observed=True, runtime_verification="not_run")
