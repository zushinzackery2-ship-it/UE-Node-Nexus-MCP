"""Shared helpers for counting error/warning diagnostics in bridge responses.

Single home for the severity-count logic previously duplicated across
``runtime``, ``response_normalization``, and ``facade_response``.
"""

from __future__ import annotations

from typing import Any

_ERROR_SEVERITIES = {"error", "fatal"}


def coerce_error_count(value: Any) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def count_diagnostic_errors(items: Any) -> int:
    if not isinstance(items, list):
        return 0
    total = 0
    for item in items:
        if not isinstance(item, dict):
            continue
        if str(item.get("severity", "")).strip().lower() in _ERROR_SEVERITIES:
            total += 1
    return total


def count_diagnostic_warnings(items: Any) -> int:
    if not isinstance(items, list):
        return 0
    total = 0
    for item in items:
        if not isinstance(item, dict):
            continue
        if str(item.get("severity", "")).strip().lower() == "warning":
            total += 1
    return total
