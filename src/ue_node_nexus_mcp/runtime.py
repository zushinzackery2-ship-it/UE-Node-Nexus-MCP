from __future__ import annotations

import os
import sys
from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge import BridgeError, UeBridgeClient
from .contracts import DEFAULT_FEATURE_GROUPS, DEFAULT_HIDDEN_OPERATIONS, FEATURE_GROUPS, OPERATION_FEATURES

mcp = FastMCP("UE Node Nexus MCP")
bridge = UeBridgeClient()
_enabled_features = None


def _parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "off", "disable", "disabled"}:
        return False
    raise ValueError(f"invalid boolean value: {value}")


def _parse_feature_list(value: str) -> set[str]:
    features = {part.strip().lower() for part in value.replace(";", ",").split(",") if part.strip()}
    unknown = features - FEATURE_GROUPS
    if unknown:
        raise ValueError(f"unknown feature group(s): {', '.join(sorted(unknown))}")
    return features


def _consume_feature_args(argv: list[str]) -> tuple[set[str] | None, set[str], set[str], bool | None, list[str]]:
    explicit_features = None
    enable_features: set[str] = set()
    disable_features: set[str] = set()
    niagara_support = None
    remaining = [argv[0]]
    index = 1
    while index < len(argv):
        arg = argv[index]
        if arg == "--features":
            index += 1
            if index >= len(argv):
                raise ValueError("--features requires a comma-separated feature list")
            explicit_features = _parse_feature_list(argv[index])
        elif arg.startswith("--features="):
            explicit_features = _parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--enable-feature":
            index += 1
            if index >= len(argv):
                raise ValueError("--enable-feature requires a feature name")
            enable_features |= _parse_feature_list(argv[index])
        elif arg.startswith("--enable-feature="):
            enable_features |= _parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--disable-feature":
            index += 1
            if index >= len(argv):
                raise ValueError("--disable-feature requires a feature name")
            disable_features |= _parse_feature_list(argv[index])
        elif arg.startswith("--disable-feature="):
            disable_features |= _parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--niagara-support":
            index += 1
            if index >= len(argv):
                raise ValueError("--niagara-support requires true or false")
            niagara_support = _parse_bool(argv[index])
        elif arg.startswith("--niagara-support="):
            niagara_support = _parse_bool(arg.split("=", 1)[1])
        else:
            remaining.append(arg)
        index += 1
    return explicit_features, enable_features, disable_features, niagara_support, remaining


def _read_enabled_features() -> set[str]:
    explicit_features = None
    enable_features: set[str] = set()
    disable_features: set[str] = set()
    niagara_support = None

    env_features = os.getenv("UE_NEXUS_FEATURES")
    if env_features:
        explicit_features = _parse_feature_list(env_features)

    env_enable = os.getenv("UE_NEXUS_ENABLE_FEATURES")
    if env_enable:
        enable_features |= _parse_feature_list(env_enable)

    env_disable = os.getenv("UE_NEXUS_DISABLE_FEATURES")
    if env_disable:
        disable_features |= _parse_feature_list(env_disable)

    env_niagara = os.getenv("UE_NEXUS_NIAGARA_SUPPORT")
    if env_niagara:
        niagara_support = _parse_bool(env_niagara)

    arg_features, arg_enable, arg_disable, arg_niagara, remaining = _consume_feature_args(sys.argv)
    sys.argv[:] = remaining
    if arg_features is not None:
        explicit_features = arg_features
    enable_features |= arg_enable
    disable_features |= arg_disable
    if arg_niagara is not None:
        niagara_support = arg_niagara

    features = set(explicit_features if explicit_features is not None else DEFAULT_FEATURE_GROUPS)
    features |= enable_features
    features -= disable_features
    if niagara_support is True:
        features.add("niagara")
    elif niagara_support is False:
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
        if key in {"remaining_errors", "error_count"}:
            total += _coerce_error_count(child)
            continue
        if key == "error" and isinstance(child, dict):
            total += 1
            continue
        total += _count_nested_errors(child)
    return total


def _count_response_errors(response: dict[str, Any]) -> int:
    total = _count_nested_errors(response)
    if response.get("ok") is False and total == 0:
        return 1
    return total


def _with_remaining_errors(response: dict[str, Any]) -> dict[str, Any]:
    response.pop("remaining_errors", None)
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
