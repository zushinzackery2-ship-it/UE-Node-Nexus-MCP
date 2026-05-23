from __future__ import annotations

import os
import sys
from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient
from .contracts import DEFAULT_HIDDEN_OPERATIONS, FEATURE_GROUPS, OPERATION_FEATURES
from .features import consume_feature_args, read_feature_env, resolve_enabled_features

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()
_enabled_features = None
_CAPABILITY_PROBE_TIMEOUT_SECONDS = 1.0


def _bridge_niagara_available() -> bool:
    try:
        response = bridge.call(
            "bridge_capabilities_get",
            {},
            timeout_seconds=_CAPABILITY_PROBE_TIMEOUT_SECONDS,
        )
    except (BridgeError, OSError, TimeoutError, ValueError):
        return False

    data = response.get("data")
    if not isinstance(data, dict):
        return False
    modules = data.get("modules")
    if not isinstance(modules, dict):
        return False
    return modules.get("niagara_available") is True


def _read_enabled_features() -> set[str]:
    explicit_features, enable_features, disable_features, niagara_support = read_feature_env(dict(os.environ))
    arg_features, arg_enable, arg_disable, arg_niagara, remaining = consume_feature_args(sys.argv)
    sys.argv[:] = remaining
    if arg_features is not None:
        explicit_features = arg_features
    enable_features |= arg_enable
    disable_features |= arg_disable
    if arg_niagara is not None:
        niagara_support = arg_niagara

    features = resolve_enabled_features(explicit_features, enable_features, disable_features, niagara_support)
    if "niagara" in features and not _bridge_niagara_available():
        features.discard("niagara")
    return features


def enabled_features() -> set[str]:
    global _enabled_features
    if _enabled_features is None:
        _enabled_features = _read_enabled_features()
    return set(_enabled_features)


def is_feature_enabled(feature: str) -> bool:
    if feature not in FEATURE_GROUPS:
        raise ValueError(f"unknown feature group: {feature}")
    return feature in enabled_features()


def _operation_feature(operation: str) -> str:
    try:
        return OPERATION_FEATURES[operation]
    except KeyError as exc:
        raise ValueError(f"{operation} has no feature group mapping") from exc


def default_tool(feature: str | None = None):
    def decorator(func):
        tool_feature = feature or _operation_feature(func.__name__)
        if is_feature_enabled(tool_feature):
            return mcp.tool()(func)
        return func

    return decorator


def hidden_tool(feature: str | None = None):
    """Validate hidden compatibility tools without registering them in MCP list_tools."""

    def decorator(func):
        if func.__name__ not in DEFAULT_HIDDEN_OPERATIONS:
            raise ValueError(f"{func.__name__} is not declared as a default-hidden operation")
        tool_feature = feature or _operation_feature(func.__name__)
        if tool_feature not in FEATURE_GROUPS:
            raise ValueError(f"unknown feature group: {tool_feature}")
        return func

    return decorator


def _coerce_error_count(value: Any) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def _count_diagnostic_errors(items: Any) -> int:
    if not isinstance(items, list):
        return 0
    total = 0
    for item in items:
        if not isinstance(item, dict):
            continue
        severity = str(item.get("severity", "")).strip().lower()
        if severity in {"error", "fatal"}:
            total += 1
    return total


def _count_nested_errors(value: Any) -> int:
    if isinstance(value, list):
        return sum(_count_nested_errors(item) for item in value)
    if not isinstance(value, dict):
        return 0

    total = 0
    for key, child in value.items():
        if key == "diagnostics":
            total += _count_diagnostic_errors(child)
            continue
        if key == "error_count":
            total += _coerce_error_count(child)
            continue
        if key == "error" and isinstance(child, dict):
            total += 1
            continue
        total += _count_nested_errors(child)
    return total


def _strip_nested_remaining_errors(value: Any) -> None:
    if isinstance(value, list):
        for item in value:
            _strip_nested_remaining_errors(item)
        return
    if not isinstance(value, dict):
        return

    for key in list(value):
        if key == "remaining_errors":
            value.pop(key, None)
            continue
        _strip_nested_remaining_errors(value[key])


def _count_response_errors(response: dict[str, Any]) -> int:
    total = _count_nested_errors(response)
    if response.get("ok") is False and total == 0:
        return 1
    return total


def _with_remaining_errors(response: dict[str, Any]) -> dict[str, Any]:
    response.pop("remaining_errors", None)
    _strip_nested_remaining_errors(response)
    response["remaining_errors"] = _count_response_errors(response)
    return response


def call_bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    try:
        return _with_remaining_errors(bridge.call(operation, payload))
    except (BridgeError, ValueError) as exc:
        return _with_remaining_errors({
            "ok": False,
            "operation": operation,
            "error": {
                "code": "mcp_bridge_error",
                "message": str(exc),
                "details": {},
            },
            "diagnostics": [],
            "warnings": [],
        })
