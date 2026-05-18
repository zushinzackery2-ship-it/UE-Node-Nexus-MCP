from __future__ import annotations

import json
from typing import Any

import pytest

from ue_node_nexus_mcp.bridge import BridgeConfig, BridgeError, UeBridgeClient


class FakeBridgeClient(UeBridgeClient):
    def __init__(self, response: dict[str, Any] | list[Any]) -> None:
        super().__init__(BridgeConfig(base_url="http://127.0.0.1:1", timeout_seconds=1))
        self.last_path = ""
        self.last_body: dict[str, Any] = {}
        self.response = response

    def _post_json(self, path: str, body: dict[str, Any]) -> dict[str, Any]:
        self.last_path = path
        self.last_body = json.loads(json.dumps(body))
        if not isinstance(self.response, dict):
            raise BridgeError("Bridge returned JSON that is not an object")
        return self.response


def test_bridge_wraps_operation_envelope() -> None:
    client = FakeBridgeClient({"ok": True, "data": {"items": []}})

    result = client.call("asset_list", {"limit": 1})

    assert result["ok"] is True
    assert result["operation"] == "asset_list"
    assert result["request_id"] == client.last_body["request_id"]
    assert client.last_path == "/mcp"
    assert client.last_body["payload"] == {"limit": 1}


def test_bridge_rejects_unknown_operation() -> None:
    client = FakeBridgeClient({"ok": True})

    with pytest.raises(ValueError):
        client.call("python_exec", {})

