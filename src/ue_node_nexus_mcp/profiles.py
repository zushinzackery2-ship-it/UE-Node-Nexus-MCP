from __future__ import annotations

import os


PROFILE_LEGACY = "legacy"
PROFILE_THIN = "thin"
VALID_MCP_PROFILES = {PROFILE_LEGACY, PROFILE_THIN}
DEFAULT_MCP_PROFILE = PROFILE_LEGACY

RESPONSE_MINIMAL = "minimal"
RESPONSE_FULL = "full"
VALID_RESPONSE_MODES = {RESPONSE_MINIMAL, RESPONSE_FULL}
DEFAULT_RESPONSE_MODE = RESPONSE_MINIMAL


class ProfileArgumentError(ValueError):
    pass


def parse_mcp_profile(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in VALID_MCP_PROFILES:
        raise ProfileArgumentError(f"unknown MCP profile: {value}")
    return normalized


def parse_response_mode(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in VALID_RESPONSE_MODES:
        raise ProfileArgumentError(f"unknown response mode: {value}")
    return normalized


def consume_profile_args(argv: list[str]) -> tuple[str | None, str | None, list[str]]:
    profile = None
    response_mode = None
    remaining = [argv[0]]
    index = 1
    while index < len(argv):
        arg = argv[index]
        if arg == "--mcp-profile":
            index += 1
            if index >= len(argv):
                raise ProfileArgumentError("--mcp-profile requires thin or legacy")
            profile = parse_mcp_profile(argv[index])
        elif arg.startswith("--mcp-profile="):
            profile = parse_mcp_profile(arg.split("=", 1)[1])
        elif arg == "--response-mode":
            index += 1
            if index >= len(argv):
                raise ProfileArgumentError("--response-mode requires minimal or full")
            response_mode = parse_response_mode(argv[index])
        elif arg.startswith("--response-mode="):
            response_mode = parse_response_mode(arg.split("=", 1)[1])
        else:
            remaining.append(arg)
        index += 1
    return profile, response_mode, remaining


def read_profile_env(env: dict[str, str]) -> tuple[str | None, str | None]:
    profile = None
    response_mode = None

    env_profile = env.get("UE_NEXUS_MCP_PROFILE")
    if env_profile:
        profile = parse_mcp_profile(env_profile)

    env_response = env.get("UE_NEXUS_RESPONSE_MODE")
    if env_response:
        response_mode = parse_response_mode(env_response)

    return profile, response_mode
