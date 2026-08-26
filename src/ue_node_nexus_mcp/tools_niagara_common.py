from __future__ import annotations

from typing import Any

from .runtime import call_bridge as _call

# Bridge-side Niagara responses embed static capability prose that costs model
# context on every call; the facade strips it before the response is returned.
VERBOSE_NIAGARA_DATA_FIELDS = {
    "capabilities",
    "limitations",
    "recommended_generic_workflow",
}


def trim_verbose_niagara_fields(response: dict[str, Any]) -> dict[str, Any]:
    data = response.get("data")
    if isinstance(data, dict):
        for field in VERBOSE_NIAGARA_DATA_FIELDS:
            data.pop(field, None)
    return response


def call_niagara_trimmed(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    """Facade entrypoint for all niagara_* bridge operations."""
    return trim_verbose_niagara_fields(_call(operation, payload))


# Legacy wrapper alias so tools_niagara* modules keep one shared call helper.
_call_niagara = call_niagara_trimmed
