from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.contracts import FEATURE_GROUPS


def pytest_configure(config: pytest.Config) -> None:
    if config.option.basetemp is None:
        config.option.basetemp = str(_space_root(config))


def _space_root(config: pytest.Config) -> Path:
    """Use the explicit test volume or the workspace, preserving disk admission."""
    from ue_node_nexus_mcp.safety.isolation.snapshot import WORKING_BYTES

    required = WORKING_BYTES + 64 * 1024 ** 2
    configured = os.environ.get("UE_NEXUS_TEST_SPACE_DIR")
    directory = Path(configured) if configured else config.rootpath / "build"
    if not directory.is_dir() or shutil.disk_usage(directory).free < required:
        raise pytest.UsageError(f"test volume requires {required} bytes; set UE_NEXUS_TEST_SPACE_DIR to an existing NTFS directory on Windows")
    return directory / "nexus-pytest-temp"


@pytest.fixture
def all_features(monkeypatch: pytest.MonkeyPatch) -> None:
    """Force every feature group enabled and skip CLI/env/probe resolution."""
    monkeypatch.setattr(runtime, "_enabled_features", set(FEATURE_GROUPS))
    monkeypatch.setattr(runtime, "_response_mode", "minimal")
    monkeypatch.setattr(runtime, "_profile_args_consumed", True)


@pytest.fixture
def use_bridge(monkeypatch: pytest.MonkeyPatch) -> Callable[[Any], Any]:
    def _install(bridge: Any) -> Any:
        from ue_node_nexus_mcp.instances.session import instance_manager
        from tests.support.scopes import Scope
        monkeypatch.setattr(runtime, "bridge", bridge)
        monkeypatch.setattr(instance_manager, "reserve", Scope)
        return bridge

    return _install
