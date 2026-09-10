"""Check the loaded DLL contract before dispatching editor mutations."""

from __future__ import annotations

import re
import threading
import time
from typing import Any, Callable

from ..contracts import OPERATION_FEATURES, WRITE_OPERATIONS

VERSION = "0.4.0"
CONTRACT_VERSION = 2
CORE_MODULE = "UeNodeNexusBridge"
VFX_MODULE = "UeNodeNexusVfxBridge"


def compatibility(data: dict[str, Any], operation: str) -> dict[str, Any] | None:
    modules = data.get("build")
    required = [CORE_MODULE, VFX_MODULE] if OPERATION_FEATURES.get(operation) == "vfx" else [CORE_MODULE]
    if not isinstance(modules, dict):
        return dict(code="bridge_build_identity_missing", message="The loaded plugin does not expose build identity; install the matching plugins and restart the editor.")
    for name in required:
        module = modules.get(name) or dict()
        valid = (module.get("version") == VERSION and module.get("contract_version") == CONTRACT_VERSION
                 and module.get("loaded") is True and bool(module.get("build_id")) and bool(module.get("module_path"))
                 and re.fullmatch(r"[a-fA-F0-9]{64}", str(module.get("source_fingerprint", ""))))
        if not valid:
            return dict(code="bridge_contract_mismatch", message=f"Python {VERSION} requires {name} {VERSION} with contract {CONTRACT_VERSION}.",
                        details=dict(module=name, expected_version=VERSION, expected_contract=CONTRACT_VERSION, loaded=module))
    if len(required) == 2 and modules[CORE_MODULE]["build_id"] != modules[VFX_MODULE]["build_id"]:
        return dict(code="bridge_build_id_mismatch", message="Core and VFX module manifests have different engine BuildIds.")
    return None


class ContractCache:
    def __init__(self) -> None:
        self._entries: dict[str, tuple[float, dict[str, Any]]] = dict()
        self._lock = threading.RLock()

    def observe(self, target: str, response: dict[str, Any]) -> None:
        if response.get("ok") and isinstance(response.get("data"), dict):
            with self._lock:
                self._entries[target] = (time.monotonic(), response["data"])

    def invalidate(self, target: str) -> None:
        with self._lock:
            self._entries.pop(target, None)

    def check(self, target: str, operation: str, payload: dict[str, Any],
              fetch: Callable[[], dict[str, Any]]) -> dict[str, Any] | None:
        needs_identity = (operation in WRITE_OPERATIONS and payload.get("dry_run") is not True)
        needs_identity = needs_identity or operation in ("scene_export", "scene_status", "component_instances_get")
        if not needs_identity:
            return None
        with self._lock:
            stamp, data = self._entries.get(target, (0.0, dict()))
            if not data or time.monotonic() - stamp >= 5.0:
                response = fetch()
                if not response.get("ok"):
                    return response.get("error") or dict(code="bridge_contract_unavailable", message="Could not read the loaded plugin identity.")
                data = response.get("data") or dict()
                self._entries[target] = (time.monotonic(), data)
            return compatibility(data, operation)
