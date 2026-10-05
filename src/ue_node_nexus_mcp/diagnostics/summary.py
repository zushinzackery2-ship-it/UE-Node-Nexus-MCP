"""Preserve diagnostic evidence through compact MCP responses."""

from ..diagnostic_counting import coerce_error_count
from .contracts.response import attach as attach_contract
from .contracts.samples import ranked


FIELDS = ("error_count", "warning_count", "returned_error_count", "returned_warning_count", "item_count", "scope", "coverage", "inspection_mode",
          "assets_scanned", "assets_supported", "assets_unsupported", "assets_not_loaded", "runtime",
          "related_log_item_count", "related_log_source", "related_log_stale_possible")


def counts(response: dict) -> tuple[int, int]:
    data = response.get("data")
    if not isinstance(data, dict) or "items" not in data:
        return 0, 0
    return coerce_error_count(data.get("error_count")), coerce_error_count(data.get("warning_count"))


def attach(summary: dict, response: dict, operation: str, payload: dict) -> dict:
    attach_contract(summary, response, operation)
    if operation != "diagnostics_get":
        return summary
    data = response.get("data", dict())
    for field in FIELDS:
        if field in data:
            summary[field] = data[field]
    summary["items"] = ranked(data.get("items", []))
    summary["related_log_items"] = ranked(data.get("related_log_items", []))
    if not summary.get("stored_as_artifact"):
        summary["next_read"] = dict(tool="ue_read", args=dict(target="diagnostics", query=payload, format="detail"))
    return summary
