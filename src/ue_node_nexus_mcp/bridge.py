from __future__ import annotations

import os
import logging
import time
import uuid
from dataclasses import dataclass
from typing import Any

from .contracts import BRIDGE_OPERATIONS
from .build_info.contract import ContractCache
from .errors import BridgeError
from .instance import instance_manager
from .transport import named_pipe_transport

# Re-exported so existing callers keep importing it from this module.
__all__ = ["BridgeConfig", "BridgeError", "UeBridgeClient"]
LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True)
class BridgeConfig:
    timeout_seconds: float

    @staticmethod
    def from_environment() -> BridgeConfig:
        return BridgeConfig(
            timeout_seconds=float(os.environ.get("UE_NEXUS_TIMEOUT_SECONDS", "30")),
        )


class UeBridgeClient:
    """Forwards bridge operations to the session's selected UE editor instance
    over its named pipe. Instance selection and the wire transport are injected
    so tests can swap in fakes."""

    def __init__(
        self,
        config: BridgeConfig | None = None,
        transport: Any | None = None,
        instances: Any | None = None,
    ) -> None:
        self._config = config or BridgeConfig.from_environment()
        self._transport = transport or named_pipe_transport
        self._instances = instances or instance_manager
        self._contracts = ContractCache()

    def call(self, operation: str, payload: dict[str, Any], timeout_seconds: float | None = None) -> dict[str, Any]:
        if operation not in BRIDGE_OPERATIONS:
            raise ValueError(f"Unsupported operation: {operation}")

        request_id = str(uuid.uuid4())
        envelope = {
            "operation": operation,
            "request_id": request_id,
            "payload": payload,
        }
        timeout = self._config.timeout_seconds if timeout_seconds is None else timeout_seconds
        target = self._instances.resolve_target()
        error = self._contracts.check(target, operation, payload, lambda: self._send(
            target, dict(operation="bridge_capabilities_get", request_id=str(uuid.uuid4()), payload=dict()), timeout))
        if error:
            return dict(ok=False, operation=operation, request_id=request_id, error=error, diagnostics=[], warnings=[])
        response = self._send(target, envelope, timeout)
        if operation == "bridge_capabilities_get":
            self._contracts.observe(target, response)

        response.setdefault("operation", operation)
        response.setdefault("request_id", request_id)
        response.setdefault("diagnostics", [])
        response.setdefault("warnings", [])
        return response

    def _send(self, target: str, envelope: dict[str, Any], timeout: float) -> dict[str, Any]:
        started = time.monotonic()
        LOGGER.info("request=%s operation=%s phase=begin target=%s", envelope["request_id"], envelope["operation"], target)
        try:
            response = self._transport.send(target, envelope, timeout)
        except (BridgeError, OSError):
            self._contracts.invalidate(target)
            LOGGER.exception("request=%s phase=transport_failed", envelope["request_id"])
            raise

        if not isinstance(response, dict):
            raise BridgeError("Bridge returned a non-object response")

        LOGGER.info("request=%s phase=end ok=%s duration_ms=%.3f", envelope["request_id"], response.get("ok"), (time.monotonic() - started) * 1000.0)
        return response
