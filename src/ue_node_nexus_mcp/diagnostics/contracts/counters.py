"""Map known operation and post-check counters instead of scanning arbitrary JSON."""

from ...contracts import WRITE_OPERATIONS
from ...diagnostic_counting import coerce_error_count, count_diagnostic_errors, count_diagnostic_warnings


def operation_counts(response: dict) -> tuple[int, int]:
    data = response.get("data")
    data = data if isinstance(data, dict) else dict()
    diagnostics = [*(response.get("diagnostics") or []), *(data.get("diagnostics") or [])]
    errors = count_diagnostic_errors(diagnostics)
    warnings = count_diagnostic_warnings(diagnostics) + len(response.get("warnings") or [])
    for block in (data.get("compile"), (data.get("post_checks") or dict()).get("compile")):
        if isinstance(block, dict):
            errors = max(errors, coerce_error_count(block.get("error_count")))
            warnings = max(warnings, coerce_error_count(block.get("warning_count")))
    if data.get("action") or data.get("apply_id") or data.get("applied") is not None:
        errors = max(errors, coerce_error_count(data.get("error_count")))
    if response.get("ok") is False and isinstance(response.get("error"), dict):
        errors = max(errors, 1)
    return errors, warnings


def operation_notice(response: dict, operation: str) -> dict:
    existing = response.get("operation_diagnostics")
    if isinstance(existing, dict):
        return dict(existing)
    errors, warnings = operation_counts(response)
    return dict(scope="operation", operation=operation, error_count=errors, warning_count=warnings,
                status="failed" if response.get("ok") is False or errors else "ok")


def postcheck(operation: str, payload: dict, response: dict) -> dict:
    response.pop("remaining_errors", None)
    asset = payload.get("asset_path")
    data = response.get("data")
    data = data if isinstance(data, dict) else dict()
    checks = data.get("post_checks")
    has_checks = isinstance(checks, dict) or isinstance(data.get("compile"), dict) or operation == "asset_compile"
    if operation in WRITE_OPERATIONS and asset and payload.get("dry_run") is not True and has_checks:
        errors, _ = operation_counts(response)
        response["remaining_errors"] = errors
        response["remaining_errors_scope"] = dict(asset_path=asset, stage="asset_postcheck")
    return response
