from __future__ import annotations

import pytest

from ue_node_nexus_mcp.profiles import (
    ProfileArgumentError,
    consume_profile_args,
    parse_mcp_profile,
    parse_response_mode,
    read_profile_env,
)


def test_parse_mcp_profile_accepts_known_profiles() -> None:
    assert parse_mcp_profile("thin") == "thin"
    assert parse_mcp_profile(" legacy ") == "legacy"


def test_parse_mcp_profile_rejects_unknown_profile() -> None:
    with pytest.raises(ProfileArgumentError):
        parse_mcp_profile("all")


def test_parse_response_mode_accepts_known_modes() -> None:
    assert parse_response_mode("minimal") == "minimal"
    assert parse_response_mode(" full ") == "full"


def test_consume_profile_args_strips_profile_flags() -> None:
    profile, response, remaining = consume_profile_args([
        "tool.py",
        "--mcp-profile",
        "thin",
        "--response-mode=full",
        "--stdio",
    ])

    assert profile == "thin"
    assert response == "full"
    assert remaining == ["tool.py", "--stdio"]


def test_read_profile_env_reads_profile_and_response_mode() -> None:
    profile, response = read_profile_env({
        "UE_NEXUS_MCP_PROFILE": "thin",
        "UE_NEXUS_RESPONSE_MODE": "minimal",
    })

    assert profile == "thin"
    assert response == "minimal"
