from __future__ import annotations

import os
import sys
import time
from typing import Any, get_type_hints

try:
    from mcp.server.fastmcp import FastMCP
except ModuleNotFoundError:  # MCP 2.x renamed FastMCP to MCPServer.
    from mcp.server import MCPServer as FastMCP

from .bridge import BridgeError, UeBridgeClient
from .contracts import DEFAULT_HIDDEN_OPERATIONS, FEATURE_GROUPS, OPERATION_FEATURES
from .diagnostic_counting import coerce_error_count, count_diagnostic_errors
from .features import consume_feature_args, read_feature_env, resolve_enabled_features
from .instances.session import instance_manager
from .instances.errors import InstanceError
from .instances.session.lifespan import lifespan, configure as configure_session
from .profiles import (
    DEFAULT_RESPONSE_MODE,
    consume_profile_args,
    read_profile_env,
)
from .response_normalization import normalize_bridge_response

mcp = FastMCP("UE Node Nexus MCP", lifespan=lifespan)
bridge = UeBridgeClient()
_enabled_features = None
_response_mode = None
_profile_args_consumed = False
_cli_feature_args: tuple[set[str] | None, set[str], set[str], bool | None] | None = None


def _bridge_vfx_available() -> bool | None:
    """Probe the bound editor for VFX module availability.

    Returns True/False for a definitive editor answer, or None when the probe
    was inconclusive (no editor reachable yet); inconclusive results must not
    be cached so a later-started editor can still enable the vfx group.
    """
    return instance_manager.current().get("vfx_available")


def consume_cli_arguments() -> None:
    """Parse and remove this server's CLI arguments from sys.argv.

    Called eagerly from ``server.main()`` so argv handling happens at startup;
    lazy callers (tests, direct imports) hit the same idempotent path on first
    feature/profile access.
    """
    global _cli_feature_args
    configure_session()
    _ensure_profile_args_consumed()
    if _cli_feature_args is None:
        arg_features, arg_enable, arg_disable, arg_vfx, remaining = consume_feature_args(sys.argv)
        sys.argv[:] = remaining
        _cli_feature_args = (arg_features, arg_enable, arg_disable, arg_vfx)


def _read_enabled_features() -> tuple[set[str], bool]:
    """Resolve the enabled feature groups and whether the result is cacheable."""
    explicit_features, enable_features, disable_features, vfx_support = read_feature_env(dict(os.environ))
    consume_cli_arguments()
    arg_features, arg_enable, arg_disable, arg_vfx = _cli_feature_args or (None, set(), set(), None)
    if arg_features is not None:
        explicit_features = arg_features
    enable_features |= arg_enable
    disable_features |= arg_disable
    if arg_vfx is not None:
        vfx_support = arg_vfx

    features = resolve_enabled_features(explicit_features, enable_features, disable_features, vfx_support)
    cacheable = True
    if "vfx" in features:
        vfx_available = _bridge_vfx_available()
        if vfx_available is None:
            features.discard("vfx")
            cacheable = False
        elif not vfx_available:
            features.discard("vfx")
    return features, cacheable


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
    if _enabled_features is not None:
        return set(_enabled_features)
    features, cacheable = _read_enabled_features()
    if cacheable:
        _enabled_features = features
    return set(features)


def reset_feature_cache() -> None:
    """Drop cached feature gating so it is recomputed against the active editor
    instance. Called when the session binds to a (new) editor instance, since
    different editors may have different module availability (e.g. niagara)."""
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
            maximum = max(maximum, coerce_error_count(child))
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
    diagnostic_total = count_diagnostic_errors(response.get("diagnostics"))
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


def _with_remaining_errors(operation: str, payload: dict[str, Any], response: dict[str, Any]) -> dict[str, Any]:
    response.pop("remaining_errors", None)
    _strip_nested_remaining_errors(response)
    if _payload_asset_path(payload) is not None:
        response["remaining_errors"] = _count_response_errors(response)
    return response


_BUSY_RETRIES = 8
_BUSY_RETRY_SECONDS = 0.25


def _is_busy(response: dict[str, Any]) -> bool:
    error = response.get("error") if isinstance(response, dict) else None
    return isinstance(error, dict) and error.get("code") == "bridge_busy"


def call_bridge(operation: str, payload: dict[str, Any]) -> dict[str, Any]:
    try:
        response = bridge.call(operation, payload)
        # the editor refuses to nest requests while one is still executing (see RequestDispatch.cpp)
        for _ in range(_BUSY_RETRIES):
            if not _is_busy(response):
                break
            time.sleep(_BUSY_RETRY_SECONDS)
            response = bridge.call(operation, payload)
        return _with_remaining_errors(operation, payload, normalize_bridge_response(response))
    except (BridgeError, ValueError) as exc:
        if isinstance(exc, InstanceError):
            return _with_remaining_errors(operation, payload, dict(exc.envelope(), operation=operation, diagnostics=[], warnings=[]))
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


# Feature gating is per-editor; recompute it whenever the session (re)binds to
# an instance. The session SDK remains independent of runtime.
instance_manager.set_on_bind_changed(reset_feature_cache)
