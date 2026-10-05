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


def thin_tool(offload: bool = False):
    def decorator(func):
        # FastMCP 1.13 calls issubclass() directly on annotations and therefore
        # cannot consume the strings produced by ``from __future__ import
        # annotations``. Resolve them once before registration; MCP 2.x accepts
        # the resulting runtime types as well.
        func.__annotations__ = get_type_hints(func)
        from .diagnostics.contracts.observation import tool_wrapper

        func = tool_wrapper(func)
        if offload:
            from asyncio import to_thread
            from functools import wraps

            @wraps(func)
            async def invoke(*args, **kwargs):
                return await to_thread(func, *args, **kwargs)

            mcp.tool()(invoke)
            return func
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


def _with_remaining_errors(operation: str, payload: dict[str, Any], response: dict[str, Any]) -> dict[str, Any]:
    from .diagnostics.contracts.counters import postcheck
    from .diagnostics.contracts.observation import record
    from .diagnostics.contracts.response import attach

    postcheck(operation, payload, response)
    attach(response, response, operation)
    record(response)
    return response


_BUSY_RETRIES = 8
_BUSY_RETRY_SECONDS = 0.25


def _is_busy(response: dict[str, Any]) -> bool:
    error = response.get("error") if isinstance(response, dict) else None
    return isinstance(error, dict) and error.get("code") == "bridge_busy"


def call_bridge(operation: str, payload: dict[str, Any], *, client: Any | None = None) -> dict[str, Any]:
    selected = bridge if client is None else client
    try:
        response = selected.call(operation, payload)
        # the editor refuses to nest requests while one is still executing (see RequestDispatch.cpp)
        for _ in range(_BUSY_RETRIES):
            if not _is_busy(response):
                break
            time.sleep(_BUSY_RETRY_SECONDS)
            response = selected.call(operation, payload)
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
