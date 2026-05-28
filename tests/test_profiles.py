from __future__ import annotations

import pytest

from ue_node_nexus_mcp.profiles import (
    ProfileArgumentError,
    consume_profile_args,
    parse_response_mode,
    read_profile_env,
)


def test_parse_response_mode_accepts_known_modes() -> None:
    assert parse_response_mode("minimal") == "minimal"
    assert parse_response_mode(" full ") == "full"


def test_consume_profile_args_strips_response_flags() -> None:
    response, remaining = consume_profile_args([
        "tool.py",
        "--response-mode=full",
        "--stdio",
    ])

    assert response == "full"
    assert remaining == ["tool.py", "--stdio"]


def test_read_profile_env_reads_response_mode() -> None:
    response = read_profile_env({
        "UE_NEXUS_RESPONSE_MODE": "minimal",
    })

    assert response == "minimal"
