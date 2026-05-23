from __future__ import annotations

from typing import Any

from .runtime import call_bridge as _call


_VERBOSE_DATA_FIELDS = {
    "capabilities",
    "limitations",
    "recommended_generic_workflow",
}


def _call_niagara(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    response = _call(operation, payload)
    data = response.get("data")
    if isinstance(data, dict):
        for field in _VERBOSE_DATA_FIELDS:
            data.pop(field, None)
    return response
