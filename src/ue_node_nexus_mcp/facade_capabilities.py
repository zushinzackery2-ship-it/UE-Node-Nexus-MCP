"""Discovery tools for the thin MCP facade."""

from __future__ import annotations

from typing import Any, Literal

from .errors import BridgeError
from .facade_response import minimal_error
from .instances.session import instance_manager
from .instances.errors import InstanceError
from .operation_registry import (
    capability_index,
    enabled_operation_specs,
    get_operation_spec,
    operation_schema,
)
from .payload_schema import example_payload_for
from .runtime import enabled_features, thin_tool
from .workflow_guides import guide_categories


@thin_tool()
def ue_context_get(include_counts: bool = True) -> dict[str, Any]:
    """Return thin facade status, enabled groups, and the recommended first capability query."""
    manager_error = None
    try:
        available_instances = instance_manager.list_instances()
    except InstanceError as exc:
        available_instances = []
        manager_error = exc.envelope()["error"]
    features = enabled_features()
    specs = enabled_operation_specs(features)
    groups: dict[str, int] = {}
    for spec in specs.values():
        if spec.hidden:
            continue
        groups[spec.group] = groups.get(spec.group, 0) + 1
    data: dict[str, Any] = {
        "bridge": "named_pipe",
        "active_instance": instance_manager.current(),
        "available_instances": available_instances,
        "groups": sorted(groups.items()) if include_counts else sorted(groups),
        "facade_tools": [
            "ue_context_get",
            "ue_capability_get",
            "ue_execute",
            "ue_read",
            "ue_diff_get",
            "ue_plan_validate",
            "ue_sync",
        ],
        "recommended_next": "ue_sync",
        "text_mirror": {
            "tool": "ue_sync",
            "edit_surface": "the .nexus text files under the mirror root are the only asset edit surface; change them with your own file tools, never with a script",
            "flow": ["checkout", "edit the returned files_root .nexus files", "stage", "commit", "push (dry_run)", "push dry_run=false"],
            "legacy_flow": ["status", "edit .nexus files", "lint", "push (dry_run)", "push dry_run=false"],
            "legacy_note": "checkout enables collaboration; init/status/lint/push then require options.workspace_id, and init is refused",
            "guides": [
                {"operation": "workflow_guide_get", "payload": {"category": "collaboration"}},
                {"operation": "workflow_guide_get", "payload": {"category": "text_mirror"}},
            ],
        },
        "workflow_guides": {
            "operation": "workflow_guide_get",
            "categories": [row["category"] for row in guide_categories()],
            "example": {"category": "getting_started"},
        },
        "background_tasks": {
            "operation": "task_submit",
            "example": {"operation": "asset_compile", "payload": {"asset_path": "/Game/Path/M_X.M_X"}},
            "poll_with": ["task_status", "task_result"],
        },
    }
    active = data["active_instance"]
    state = active.get("state")
    if not active.get("instance_id") or state == "EXITED":
        data["recommended_next"] = "ue_execute"
        data["recommended_call"] = dict(operation="bridge_instance_ensure", payload=dict(mode="reuse_only", dry_run=True))
    elif state in ("STARTING", "DRAINING", "STOPPING", "UNRESPONSIVE"):
        data["recommended_next"] = "ue_execute"
        data["recommended_call"] = dict(operation="bridge_instance_status", payload=dict(instance_id=active["instance_id"]))
    elif active.get("recovery_pending"):
        data["recommended_call"] = dict(tool="ue_sync", action="recover")
    data["instance_lifecycle"] = dict(release="bridge_instance_release", inspect="bridge_instance_status",
                                       policy="reuse shared project editor; release use leases when work is finished")
    if manager_error:
        return dict(ok=False, error=manager_error, data=data)
    return dict(ok=True, data=data)


@thin_tool()
def ue_capability_get(
    group: str | None = None,
    operation: str | None = None,
    detail: Literal["index", "schema", "examples", "full"] = "index",
    include_hidden: bool = False,
) -> dict[str, Any]:
    """Return thin facade operation index or one internal operation schema.

    High-risk compatibility operations flagged ``hidden`` in the manifest are
    omitted from the index listing unless ``include_hidden`` is set; querying
    one by name always works.
    """
    features = enabled_features()
    if group is not None and group not in features:
        return minimal_error("feature_disabled", f"feature group is not enabled: {group}", {"group": group})
    if operation:
        try:
            spec = get_operation_spec(operation)
        except ValueError as exc:
            return minimal_error("invalid_operation", str(exc), {"operation": operation})
        if spec.group not in features:
            return minimal_error(
                "feature_disabled",
                f"feature group is not enabled: {spec.group}",
                {"operation": operation},
            )
        if detail == "index":
            data: dict[str, Any] = {
                "operation": spec.name,
                "group": spec.group,
                "kind": spec.kind,
                "risk": spec.risk,
                "summary": spec.summary,
            }
        elif detail == "examples":
            data = {
                "operation": spec.name,
                "examples": [{"payload": example_payload_for(spec.name)}],
                "next_read": {
                    "tool": "ue_capability_get",
                    "args": {"operation": spec.name, "detail": "schema"},
                },
            }
        else:
            data = operation_schema(spec)
        return {"ok": True, "data": data}

    return {
        "ok": True,
        "data": {
            "group": group,
            "detail": "index",
            "operations": capability_index(features, group, include_hidden=include_hidden),
        },
    }
