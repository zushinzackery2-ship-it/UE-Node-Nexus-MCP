"""Attach the same bounded diagnostic contract to raw, shaped, and stored results."""

from .counters import operation_notice
from .notice import normalize

DIAGNOSTIC_FIELDS = ("operation_diagnostics", "diagnostic_scope", "runtime_diagnostics",
                     "runtime_observed", "runtime_verification")


def attach(target: dict, response: dict, operation: str, *, recorded: bool = False) -> dict:
    target["operation_diagnostics"] = operation_notice(response, operation)
    target["diagnostic_scope"] = "inspection" if operation == "diagnostics_get" else "operation"
    data = response.get("data")
    data = data if isinstance(data, dict) else dict()
    notice = response.get("runtime_diagnostics")
    if operation == "diagnostics_get" and isinstance(data.get("runtime"), dict):
        notice = data["runtime"]
    if not isinstance(notice, dict):
        notice = data.get("runtime_diagnostics")
    value = normalize(notice, data.get("items"), recorded=recorded)
    target["runtime_diagnostics"] = value
    target["runtime_observed"] = value["runtime_observed"]
    target.setdefault("runtime_verification", response.get("runtime_verification", data.get("runtime_verification", "not_run")))
    return target


def destination(result: dict) -> dict:
    data = result.get("data")
    if result.get("ok") is False or result.get("operation") or not isinstance(data, dict):
        return result
    return data


def finalize(result: dict, operation: str, observed: dict | None = None) -> dict:
    if not isinstance(result, dict):
        return result
    target = destination(result)
    current = target.get("runtime_diagnostics")
    if isinstance(current, dict) and current.get("session_id"):
        source = dict(result, runtime_diagnostics=current)
    else:
        source = dict(result)
        if observed:
            source["runtime_diagnostics"] = observed
    existing = target.get("operation_diagnostics")
    if isinstance(existing, dict):
        source["operation_diagnostics"] = existing
    attach(target, source, operation)
    return result
