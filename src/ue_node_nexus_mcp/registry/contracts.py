from __future__ import annotations

import json
from pathlib import Path
from typing import Any

_OPERATIONS_MANIFEST = Path(__file__).with_name("operations.json")

_VALID_RISKS = {"low", "medium", "high"}
_VALID_DEFAULT_RESPONSES = {"summary", "delta", "full"}


def _load_operation_records() -> list[dict[str, Any]]:
    with _OPERATIONS_MANIFEST.open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("version") != 2 or not isinstance(manifest.get("files"), list):
        raise ValueError("unsupported operation manifest index")
    records = []
    root = _OPERATIONS_MANIFEST.parent
    for relative in manifest["files"]:
        file = root / relative
        if not file.resolve().is_relative_to((root / "operations").resolve()):
            raise ValueError(f"operation manifest path leaves its directory: {relative}")
        with file.open(encoding="utf-8") as handle:
            records.extend(json.load(handle)["operations"])
    seen: set[str] = set()
    for record in records:
        name = record["name"]
        if name in seen:
            raise ValueError(f"duplicate operation in manifest: {name}")
        if record["kind"] not in {"read", "write"}:
            raise ValueError(f"operation {name} has invalid kind: {record['kind']}")
        if record["risk"] not in _VALID_RISKS:
            raise ValueError(f"operation {name} has invalid risk: {record['risk']}")
        if record["default_response"] not in _VALID_DEFAULT_RESPONSES:
            raise ValueError(f"operation {name} has invalid default_response: {record['default_response']}")
        summary = record["summary"]
        if not isinstance(summary, str) or not summary.strip():
            raise ValueError(f"operation {name} has an empty summary")
        seen.add(name)
    return records


_OPERATION_RECORDS = _load_operation_records()


def operation_records() -> list[dict[str, Any]]:
    """Return the raw manifest records (the single source of operation metadata)."""
    return list(_OPERATION_RECORDS)

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
    "ue_sync",
}

BRIDGE_OPERATIONS = ALL_OPERATIONS - LOCAL_MCP_OPERATIONS

DEFAULT_HIDDEN_OPERATIONS = {record["name"] for record in _OPERATION_RECORDS if record.get("hidden")}

OPERATION_FEATURES = {record["name"]: record["group"] for record in _OPERATION_RECORDS}

FEATURE_GROUPS = set(OPERATION_FEATURES.values())

DEFAULT_FEATURE_GROUPS = set(FEATURE_GROUPS)


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
