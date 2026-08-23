from __future__ import annotations

import json
from pathlib import Path
from typing import Any

_OPERATIONS_MANIFEST = Path(__file__).with_name("operations.json")


def _load_operation_records() -> list[dict[str, Any]]:
    with _OPERATIONS_MANIFEST.open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    records = manifest["operations"]
    seen: set[str] = set()
    for record in records:
        name = record["name"]
        if name in seen:
            raise ValueError(f"duplicate operation in manifest: {name}")
        if record["kind"] not in {"read", "write"}:
            raise ValueError(f"operation {name} has invalid kind: {record['kind']}")
        seen.add(name)
    return records


_OPERATION_RECORDS = _load_operation_records()

READ_OPERATIONS = {record["name"] for record in _OPERATION_RECORDS if record["kind"] == "read"}
WRITE_OPERATIONS = {record["name"] for record in _OPERATION_RECORDS if record["kind"] == "write"}

ALL_OPERATIONS = READ_OPERATIONS | WRITE_OPERATIONS

LOCAL_MCP_OPERATIONS = {record["name"] for record in _OPERATION_RECORDS if record.get("local")}

THIN_MCP_OPERATIONS = {
    "ue_capability_get",
    "ue_context_get",
    "ue_diff_get",
    "ue_execute",
    "ue_plan_validate",
    "ue_read",
}

BRIDGE_OPERATIONS = ALL_OPERATIONS - LOCAL_MCP_OPERATIONS

DEFAULT_HIDDEN_OPERATIONS = {record["name"] for record in _OPERATION_RECORDS if record.get("hidden")}

OPERATION_FEATURES = {record["name"]: record["group"] for record in _OPERATION_RECORDS}

FEATURE_GROUPS = set(OPERATION_FEATURES.values())

DEFAULT_FEATURE_GROUPS = set(FEATURE_GROUPS)

DEFAULT_EXPOSED_OPERATIONS = {
    operation
    for operation in ALL_OPERATIONS - DEFAULT_HIDDEN_OPERATIONS
    if OPERATION_FEATURES[operation] in DEFAULT_FEATURE_GROUPS
}

THIN_EXPOSED_OPERATIONS = set(THIN_MCP_OPERATIONS)


def require_non_empty_string(value: str, field_name: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{field_name} must be a non-empty string")


def require_mapping(value: dict[str, Any], field_name: str) -> None:
    if not isinstance(value, dict):
        # Public payload validation consistently reports ValueError to callers.
        raise ValueError(f"{field_name} must be an object")  # noqa: TRY004


def require_list(value: list[dict[str, Any]], field_name: str) -> None:
    if not isinstance(value, list):
        raise ValueError(f"{field_name} must be an array")  # noqa: TRY004
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ValueError(f"{field_name}[{index}] must be an object")  # noqa: TRY004


def require_exactly_one(present: dict[str, bool], group_label: str) -> None:
    """Enforce a mutually-exclusive target group.

    ``present`` maps each candidate field label to whether it was supplied.
    Raises ValueError on zero or more than one present field. Mirrors the
    authoritative C++ ``target_conflict`` / ``invalid_request`` contract for
    direct/internal callers (the live ue_execute path is validated bridge-side).
    """
    chosen = [label for label, is_present in present.items() if is_present]
    if len(chosen) == 0:
        raise ValueError(f"provide exactly one of {group_label}")
    if len(chosen) > 1:
        raise ValueError(f"provide exactly one of {group_label}; got: {', '.join(chosen)}")
