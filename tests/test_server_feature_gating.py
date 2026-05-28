from __future__ import annotations

import json
import os
import subprocess
import sys

from ue_node_nexus_mcp.contracts import THIN_EXPOSED_OPERATIONS


def _registered_tools(*args: str, env: dict[str, str] | None = None) -> set[str]:
    child_env = os.environ.copy()
    child_env["PYTHONPATH"] = "src"
    child_env.pop("UE_NEXUS_FEATURES", None)
    child_env.pop("UE_NEXUS_ENABLE_FEATURES", None)
    child_env.pop("UE_NEXUS_DISABLE_FEATURES", None)
    child_env.pop("UE_NEXUS_NIAGARA_SUPPORT", None)
    child_env.pop("UE_NEXUS_RESPONSE_MODE", None)
    if env:
        child_env.update(env)
    code = """
import json
import os
import ue_node_nexus_mcp.runtime as runtime

class FakeBridge:
    def __init__(self):
        value = os.environ.get("UE_NEXUS_TEST_NIAGARA_AVAILABLE", "true").lower()
        self.available = value in {"1", "true", "yes", "on"}

    def call(self, operation, payload, **kwargs):
        return {
            "ok": True,
            "operation": operation,
            "data": {"modules": {"niagara_available": self.available}},
            "diagnostics": [],
            "warnings": [],
        }

runtime.bridge = FakeBridge()
import ue_node_nexus_mcp.server as server
print(json.dumps(sorted(runtime.mcp._tool_manager._tools)))
"""
    result = subprocess.run(
        [sys.executable, "-c", code, *args],
        cwd=".",
        env=child_env,
        capture_output=True,
        text=True,
        check=True,
    )
    return set(json.loads(result.stdout))


def test_default_tool_surface_is_thin_facade_only() -> None:
    tools = _registered_tools()

    assert tools == THIN_EXPOSED_OPERATIONS
    assert len(tools) == 6
    assert "ue_execute" in tools
    assert "ue_read" in tools
    assert "ue_capability_get" in tools
    assert "asset_list" not in tools
    assert "bridge_capabilities_get" not in tools
    assert "bridge_contract_check" not in tools
    assert "project_context_get" not in tools
    assert "graph_build_apply" not in tools
    assert "niagara_system_create" not in tools
    assert "auto_index_clear" not in tools
    assert "editor_save_all" not in tools
    assert "editor_request_exit" not in tools


def test_response_mode_cli_arg_keeps_same_facade_tools() -> None:
    tools = _registered_tools("--response-mode", "minimal")

    assert tools == THIN_EXPOSED_OPERATIONS


def test_features_env_does_not_expand_public_tool_list() -> None:
    tools = _registered_tools(env={"UE_NEXUS_FEATURES": "core,asset"})

    assert tools == THIN_EXPOSED_OPERATIONS
    assert "asset_list" not in tools
    assert "asset_compile" not in tools
    assert "graph_build_apply" not in tools
    assert "blueprint_details_get" not in tools
    assert "material_instance_params_get" not in tools


def test_disable_feature_env_does_not_change_public_tool_list() -> None:
    tools = _registered_tools(env={"UE_NEXUS_DISABLE_FEATURES": "material,project_input"})

    assert tools == THIN_EXPOSED_OPERATIONS
    assert "material_expression_classes_list" not in tools
    assert "material_instance_params_get" not in tools
    assert "project_input_mappings_get" not in tools
    assert "asset_list" not in tools
    assert "graph_build_apply" not in tools
    assert "niagara_system_create" not in tools


def test_cli_feature_args_override_env_and_strip_server_args() -> None:
    tools = _registered_tools(
        "--features",
        "core,asset,niagara",
        "--niagara-support=false",
        env={"UE_NEXUS_FEATURES": "core,graph"},
    )

    assert tools == THIN_EXPOSED_OPERATIONS
    assert "asset_list" not in tools
    assert "asset_compile" not in tools
    assert "graph_build_apply" not in tools


def test_niagara_tools_are_internal_when_enabled() -> None:
    tools = _registered_tools(env={"UE_NEXUS_NIAGARA_SUPPORT": "true"})

    niagara_tools = {name for name in tools if name.startswith("niagara_")}
    assert tools == THIN_EXPOSED_OPERATIONS
    assert len(tools) == 6
    assert niagara_tools == set()


def test_niagara_support_false_keeps_facade_tools() -> None:
    tools = _registered_tools(env={"UE_NEXUS_NIAGARA_SUPPORT": "false"})

    assert tools == THIN_EXPOSED_OPERATIONS
    assert not {name for name in tools if name.startswith("niagara_")}


def test_ue_plugin_unavailable_keeps_facade_tools() -> None:
    tools = _registered_tools(
        env={
            "UE_NEXUS_NIAGARA_SUPPORT": "true",
            "UE_NEXUS_TEST_NIAGARA_AVAILABLE": "false",
        }
    )

    assert tools == THIN_EXPOSED_OPERATIONS
    assert not {name for name in tools if name.startswith("niagara_")}
