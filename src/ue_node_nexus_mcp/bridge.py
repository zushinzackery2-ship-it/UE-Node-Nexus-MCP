from __future__ import annotations

import json
import os
import uuid
from dataclasses import dataclass
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from .contracts import ALL_OPERATIONS


class BridgeError(RuntimeError):
    pass


@dataclass(frozen=True)
class BridgeConfig:
    base_url: str
    timeout_seconds: float

    @staticmethod
    def from_environment() -> "BridgeConfig":
        return BridgeConfig(
            base_url=os.environ.get("UE_NEXUS_BRIDGE_URL", "http://127.0.0.1:8765").rstrip("/"),
            timeout_seconds=float(os.environ.get("UE_NEXUS_TIMEOUT_SECONDS", "30")),
        )


class UeBridgeClient:
    def __init__(self, config: BridgeConfig | None = None) -> None:
        self._config = config or BridgeConfig.from_environment()

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        if operation not in ALL_OPERATIONS:
            raise ValueError(f"Unsupported operation: {operation}")

        request_id = str(uuid.uuid4())
        envelope = {
            "operation": operation,
            "request_id": request_id,
            "payload": payload,
        }
        response = self._post_json("/mcp", envelope)

        if not isinstance(response, dict):
            raise BridgeError("Bridge returned a non-object response")

        response.setdefault("operation", operation)
        response.setdefault("request_id", request_id)
        response.setdefault("diagnostics", [])
        response.setdefault("warnings", [])
        return response

    def _post_json(self, path: str, body: dict[str, Any]) -> dict[str, Any]:
        url = f"{self._config.base_url}{path}"
        data = json.dumps(body, separators=(",", ":")).encode("utf-8")
        request = Request(
            url,
            data=data,
            headers={"Content-Type": "application/json", "Accept": "application/json"},
            method="POST",
        )

        try:
            with urlopen(request, timeout=self._config.timeout_seconds) as response:
                raw = response.read().decode("utf-8")
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise BridgeError(f"Bridge HTTP error {exc.code}: {detail}") from exc
        except URLError as exc:
            raise BridgeError(f"Bridge connection failed: {exc.reason}") from exc

        try:
            decoded = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise BridgeError("Bridge returned invalid JSON") from exc

        if not isinstance(decoded, dict):
            raise BridgeError("Bridge returned JSON that is not an object")
        return decoded

