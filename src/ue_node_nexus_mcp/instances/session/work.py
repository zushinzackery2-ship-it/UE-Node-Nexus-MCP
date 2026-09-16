"""Work classification shared by batches and queued operations."""

from contextlib import nullcontext

from ...contracts import BRIDGE_OPERATIONS
from . import instance_manager


def uses_editor(operation: str, payload: dict) -> bool:
    if operation == "batch_execute":
        return any(uses_editor(item["operation"], item.get("payload", dict())) for item in payload.get("operations", []))
    return operation in (BRIDGE_OPERATIONS - set(("editor_request_exit",))) or operation in ("material_lint", "bridge_contract_check")


def exclusive(operation: str, payload: dict) -> bool:
    if operation == "batch_execute":
        return any(exclusive(item["operation"], item.get("payload", dict())) for item in payload.get("operations", []))
    return operation in ("level_open", "editor_save_all")


def reserve(operation: str, payload: dict):
    if uses_editor(operation, payload):
        return instance_manager.reserve(operation, exclusive(operation, payload), wait=False)
    return None


def operation_scope(operation: str, payload: dict):
    if uses_editor(operation, payload):
        return instance_manager.work_scope(operation, exclusive(operation, payload))
    return nullcontext()
