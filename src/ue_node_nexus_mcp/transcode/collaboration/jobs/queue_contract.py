"""Reject mixed loaded/installed schedulers before assigning durable work."""

import ast
import inspect
import logging
from pathlib import Path

from .... import __version__, task_queue
from ...errors import SyncError

LOG = logging.getLogger(__name__)


def disk_contract():
    source = Path(task_queue.__file__)
    module = ast.parse(source.read_text(encoding="utf-8"))
    function = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "submit_callable")
    names = [arg.arg for arg in (*function.args.posonlyargs, *function.args.args, *function.args.kwonlyargs)]
    package = ast.parse(source.with_name("__init__.py").read_text(encoding="utf-8"))
    version = next(node.value.value for node in package.body if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "__version__" for target in node.targets))
    return dict(path=str(source), version=version,
                supports_on_submit="on_submit" in names or function.args.kwarg is not None)


def submission_callable():
    function = task_queue.submit_callable
    signature = inspect.signature(function)
    named = signature.parameters.get("on_submit")
    accepts = named is not None and named.kind in (inspect.Parameter.POSITIONAL_OR_KEYWORD, inspect.Parameter.KEYWORD_ONLY)
    if accepts or any(item.kind == inspect.Parameter.VAR_KEYWORD for item in signature.parameters.values()):
        return function
    details = dict(loaded=dict(version=__version__, signature=str(signature), supports_on_submit=False),
                   disk=disk_contract(), required_action="restart_mcp_process")
    LOG.error("sync_scheduler_contract_mismatch loaded=%s disk=%s", details["loaded"], details["disk"])
    raise SyncError("mcp_restart_required", "the loaded MCP scheduler cannot transfer submitted work; restart the MCP process to load the installed package", details)
