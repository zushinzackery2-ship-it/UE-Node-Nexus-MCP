"""Session-scoped UE editor instance selection.

Because the MCP server runs over stdio, one Agent session == one Python process,
so this module-level ``instance_manager`` singleton is naturally per-session
state. It maps the session to exactly one editor (a target pipe name) and
arbitrates auto vs explicit selection across editor restarts.
"""

from __future__ import annotations

import uuid
from typing import Any

from .errors import BridgeError
from .transport import (
    enumerate_pipe_names,
    make_pipe_name,
    named_pipe_transport,
    parse_pid_from_pipe_name,
)

_IDENTIFY_TIMEOUT_SECONDS = 2.0


def _identify_envelope() -> dict[str, Any]:
    return {"operation": "project_context_get", "request_id": str(uuid.uuid4()), "payload": {}}


class InstanceManager:
    def __init__(self, transport: Any | None = None) -> None:
        self._transport = transport or named_pipe_transport
        self._active_target: str | None = None
        self._selection_mode: str | None = None   # "auto" | "explicit" | None
        self._on_bind_changed: Any | None = None

    def set_on_bind_changed(self, callback: Any) -> None:
        """Register a callback fired whenever the session binds to an instance.

        Injected by the runtime module (to reset per-editor feature gating)
        so this module never has to import runtime and close a cycle."""
        self._on_bind_changed = callback

    def _notify_bind_changed(self) -> None:
        if self._on_bind_changed is not None:
            self._on_bind_changed()

    def reset(self) -> None:
        self._active_target = None
        self._selection_mode = None

    def current(self) -> dict[str, Any]:
        return {
            "target": self._active_target,
            "mode": self._selection_mode,
            "pid": parse_pid_from_pipe_name(self._active_target) if self._active_target else None,
        }

    def resolve_target(self) -> str:
        """Return the pipe name for the next data-plane call, or raise BridgeError.

        Enumerates live pipes on every call so a dead bound instance is detected
        (and, when auto-selected, transparently re-bound across editor restarts).
        """
        live = dict(enumerate_pipe_names())   # {pid: pipe_name}

        if self._active_target is not None:
            bound_pid = parse_pid_from_pipe_name(self._active_target)
            if bound_pid in live:
                return self._active_target
            mode = self._selection_mode
            self.reset()
            if mode == "explicit":
                raise BridgeError(
                    f"selected UE instance (pid {bound_pid}) is gone; "
                    "call bridge_instance_list then bridge_instance_select"
                )
            # auto mode falls through to re-auto-select below

        if not live:
            raise BridgeError(
                "no UE editor instance found "
                "(is the editor running with the UeNodeNexusBridge plugin?)"
            )
        if len(live) == 1:
            ((_pid, name),) = live.items()
            self._active_target = name
            self._selection_mode = "auto"
            self._notify_bind_changed()
            return name
        raise BridgeError(
            f"multiple UE instances found (pids {sorted(live)}); "
            "call bridge_instance_list then bridge_instance_select"
        )

    def list_instances(self) -> list[dict[str, Any]]:
        instances: list[dict[str, Any]] = []
        for pid, name in sorted(enumerate_pipe_names()):
            entry: dict[str, Any] = {
                "pid": pid,
                "project_name": None,
                "project_file_path": None,
                "alive": False,
                "active": name == self._active_target,
            }
            try:
                response = self._transport.send(name, _identify_envelope(), _IDENTIFY_TIMEOUT_SECONDS)
                data = response.get("data") if isinstance(response, dict) else None
                if isinstance(data, dict):
                    entry["project_name"] = data.get("project_name")
                    entry["project_file_path"] = data.get("project_file_path")
                    entry["alive"] = True
            except BridgeError:
                entry["alive"] = False
            instances.append(entry)
        return instances

    def select(self, pid: int | None = None, project: str | None = None) -> dict[str, Any]:
        instances = self.list_instances()
        if pid is not None:
            chosen = next((item for item in instances if item["pid"] == pid), None)
            if chosen is None:
                raise BridgeError(f"no live UE instance with pid {pid}")
        elif project:
            needle = project.lower()
            matches = [item for item in instances if needle in (item.get("project_name") or "").lower()]
            if not matches:
                raise BridgeError(f"no live UE instance matching project '{project}'")
            if len(matches) > 1:
                pids = [item["pid"] for item in matches]
                raise BridgeError(f"project '{project}' matches multiple instances {pids}; select by pid")
            chosen = matches[0]
        else:
            raise BridgeError("bridge_instance_select requires 'pid' or 'project'")

        self._active_target = make_pipe_name(chosen["pid"])
        self._selection_mode = "explicit"
        chosen["active"] = True
        self._notify_bind_changed()
        return chosen


instance_manager = InstanceManager()
