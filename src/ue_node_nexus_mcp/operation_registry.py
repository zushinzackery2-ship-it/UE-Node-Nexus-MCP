from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contracts import BRIDGE_OPERATIONS, operation_records


@dataclass(frozen=True)
class OperationSpec:
    name: str
    group: str
    kind: str
    risk: str
    summary: str
    bridge_operation: str | None
    hidden: bool = False
    local_mcp: bool = False
    default_response: str = "summary"


def _build_registry() -> dict[str, OperationSpec]:
    specs: dict[str, OperationSpec] = {}
    for record in operation_records():
        name = record["name"]
        specs[name] = OperationSpec(
            name=name,
            group=record["group"],
            kind=record["kind"],
            risk=record["risk"],
            summary=record["summary"],
            bridge_operation=name if name in BRIDGE_OPERATIONS else None,
            hidden=bool(record.get("hidden")),
            local_mcp=bool(record.get("local")),
            default_response=record["default_response"],
        )
    return dict(sorted(specs.items()))


OPERATION_REGISTRY = _build_registry()


def operation_specs() -> dict[str, OperationSpec]:
    return dict(OPERATION_REGISTRY)


def get_operation_spec(operation: str) -> OperationSpec:
    try:
        return OPERATION_REGISTRY[operation]
    except KeyError as exc:
        raise ValueError(f"unknown operation: {operation}") from exc


def enabled_operation_specs(enabled_features: set[str]) -> dict[str, OperationSpec]:
    return {
        name: spec
        for name, spec in OPERATION_REGISTRY.items()
        if spec.group in enabled_features
    }


def capability_index(
    enabled_features: set[str],
    group: str | None = None,
    include_hidden: bool = False,
) -> list[list[Any]]:
    specs = list(enabled_operation_specs(enabled_features).values())
    if group:
        specs = [spec for spec in specs if spec.group == group]
    if not include_hidden:
        specs = [spec for spec in specs if not spec.hidden]
    return [
        [spec.name, spec.kind, spec.risk, spec.summary]
        for spec in sorted(specs, key=lambda item: item.name)
    ]


def operation_schema(spec: OperationSpec) -> dict[str, Any]:
    from .payload_schema import payload_schema_for

    return {
        "operation": spec.name,
        "group": spec.group,
        "kind": spec.kind,
        "risk": spec.risk,
        "summary": spec.summary,
        "bridge_operation": spec.bridge_operation,
        "default_response": spec.default_response,
        "payload_schema": payload_schema_for(spec.name),
    }
