from __future__ import annotations

import os
import sys
from typing import Any, get_type_hints

try:
    from mcp.server.fastmcp import FastMCP
except ModuleNotFoundError:  # MCP 2.x renamed FastMCP to MCPServer.
    from mcp.server import MCPServer as FastMCP

from .bridge import BridgeError, UeBridgeClient
from .contracts import DEFAULT_HIDDEN_OPERATIONS, FEATURE_GROUPS, OPERATION_FEATURES
from .features import consume_feature_args, read_feature_env, resolve_enabled_features
from .profiles import (
    DEFAULT_RESPONSE_MODE,
    consume_profile_args,
    read_profile_env,
)
from .response_normalization import normalize_bridge_response

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()
_enabled_features = None
_response_mode = None
_profile_args_consumed = False
_CAPABILITY_PROBE_TIMEOUT_SECONDS = 1.0


def _bridge_vfx_available() -> bool:
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
    return modules.get("vfx_available") is True


def _read_enabled_features() -> set[str]:
    explicit_features, enable_features, disable_features, vfx_support = read_feature_env(dict(os.environ))
    arg_features, arg_enable, arg_disable, arg_vfx, remaining = consume_feature_args(sys.argv)
    sys.argv[:] = remaining
    _ensure_profile_args_consumed()
    if arg_features is not None:
        explicit_features = arg_features
    enable_features |= arg_enable
    disable_features |= arg_disable
    if arg_vfx is not None:
        vfx_support = arg_vfx

    features = resolve_enabled_features(explicit_features, enable_features, disable_features, vfx_support)
    if "vfx" in features and not _bridge_vfx_available():
        features.discard("vfx")
    return features


def _set_cli_profile_args(response_mode: str | None) -> None:
    global _response_mode
    env_response = read_profile_env(dict(os.environ))
    _response_mode = response_mode or env_response or DEFAULT_RESPONSE_MODE


def _ensure_profile_args_consumed() -> None:
    global _profile_args_consumed
    if _profile_args_consumed:
        return
    arg_response, remaining = consume_profile_args(sys.argv)
    sys.argv[:] = remaining
    _set_cli_profile_args(arg_response)
    _profile_args_consumed = True


def enabled_features() -> set[str]:
    global _enabled_features
    if _enabled_features is None:
        _enabled_features = _read_enabled_features()
    return set(_enabled_features)


def reset_feature_cache() -> None:
    """Drop cached feature gating so it is recomputed against the active editor
    instance. Called when the selected instance changes (different editors may
    have different module availability, e.g. niagara)."""
    global _enabled_features
    _enabled_features = None


def response_mode() -> str:
    if _response_mode is None:
        _ensure_profile_args_consumed()
    return str(_response_mode)


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
    """Keep legacy wrapper functions callable as internals without exposing MCP tools."""

    def decorator(func):
        tool_feature = feature or _operation_feature(func.__name__)
        if tool_feature not in FEATURE_GROUPS:
            raise ValueError(f"unknown feature group: {tool_feature}")
        return func

    return decorator


def thin_tool():
    def decorator(func):
        # FastMCP 1.13 calls issubclass() directly on annotations and therefore
        # cannot consume the strings produced by ``from __future__ import
        # annotations``. Resolve them once before registration; MCP 2.x accepts
        # the resulting runtime types as well.
        func.__annotations__ = get_type_hints(func)
        return mcp.tool()(func)

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


def _max_nested_error_count(value: Any) -> int:
    if isinstance(value, list):
        maximum = 0
        for item in value:
            maximum = max(maximum, _max_nested_error_count(item))
        return maximum
    if not isinstance(value, dict):
        return 0

    maximum = 0
    for key, child in value.items():
        if key == "error_count":
            maximum = max(maximum, _coerce_error_count(child))
            continue
        maximum = max(maximum, _max_nested_error_count(child))
    return maximum


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
    diagnostic_total = _count_diagnostic_errors(response.get("diagnostics"))
    nested_total = _max_nested_error_count(response.get("data"))
    total = max(diagnostic_total, nested_total)
    if response.get("ok") is False and total == 0 and isinstance(response.get("error"), dict):
        return 1
    return total


def _payload_asset_path(payload: dict[str, Any]) -> str | None:
    value = payload.get("asset_path")
    if isinstance(value, str) and value:
        return value
    return None


def _should_include_remaining_errors(operation: str, payload: dict[str, Any], response: dict[str, Any]) -> bool:
    return _payload_asset_path(payload) is not None


def _with_remaining_errors(operation: str, payload: dict[str, Any], response: dict[str, Any]) -> dict[str, Any]:
    response.pop("remaining_errors", None)
    _strip_nested_remaining_errors(response)
    if _should_include_remaining_errors(operation, payload, response):
        response["remaining_errors"] = _count_response_errors(response)
    return response


def call_bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    try:
        return _with_remaining_errors(operation, payload, normalize_bridge_response(bridge.call(operation, payload)))
    except (BridgeError, ValueError) as exc:
        return _with_remaining_errors(operation, payload, {
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
