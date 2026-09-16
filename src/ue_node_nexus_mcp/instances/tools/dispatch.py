"""Lifecycle routing and structured errors, including the old exit entry point."""

from ..errors import InstanceError, require
from ..session import instance_manager


def execute(operation: str, payload: dict, session=instance_manager) -> dict:
    payload = dict((key, value) for key, value in payload.items() if value is not None)
    action = operation.removeprefix("bridge_instance_")
    try:
        if action == "ensure":
            result = session.ensure(payload)
        elif action == "list":
            result = dict(instances=session.list_instances(**payload), active=session.current(),
                          policy=dict(session.broker.policy))
        elif action == "status":
            result = session.status(payload)
        elif action == "select":
            result = dict(selected=session.select(**payload), active=session.current())
        elif action == "release":
            result = session.release(payload.get("lease_id"))
        else:
            require(action in ("close", "reap", "adopt", "pin"), "invalid_operation", "unknown instance operation")
            if action == "close" and not payload.get("instance_id"):
                payload["instance_id"] = session.current().get("instance_id")
                require(bool(payload["instance_id"]), "project_required", "select the instance to close")
            result = session.call(action, payload)
        return dict(ok=True, operation=operation, data=result, diagnostics=[], warnings=[])
    except InstanceError as exc:
        return dict(exc.envelope(), operation=operation, diagnostics=[], warnings=[])


def legacy_exit(payload: dict, session=instance_manager) -> dict:
    if payload.get("force") or payload.get("save_before_exit"):
        return dict(InstanceError("guarded_exit_required", "use bridge_instance_close with explicit save_packages; force exit is unsupported").envelope(),
                    operation="editor_request_exit", diagnostics=[], warnings=[])
    return execute("bridge_instance_close", dict(dry_run=payload.get("dry_run", True),
                   save_packages=payload.get("save_packages", []), proposal_id=payload.get("proposal_id")), session)
