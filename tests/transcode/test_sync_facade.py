from __future__ import annotations

from pathlib import Path
from contextlib import nullcontext

import pytest

from ue_node_nexus_mcp import runtime, tools_sync
from ue_node_nexus_mcp.contracts import DEFAULT_HIDDEN_OPERATIONS, THIN_MCP_OPERATIONS
from ue_node_nexus_mcp.operation_registry import get_operation_spec
from ue_node_nexus_mcp.tools_sync import ue_sync
from ue_node_nexus_mcp.workflow_guides import load_guide, search_guides
from ue_node_nexus_mcp.transcode.lifecycle import instance_manager
from ue_node_nexus_mcp.instances.errors import InstanceError

from .fake_ue import FakeUe
from .fixtures import material_function_raw


def test_facade_registers_ue_sync_and_hides_graph_ops() -> None:
    assert "ue_sync" in THIN_MCP_OPERATIONS
    assert {"graph_patch_apply", "node_params_set", "material_instance_params_set", "blueprint_components_patch", "niagara_module_add"} <= DEFAULT_HIDDEN_OPERATIONS
    assert "niagara_compile" not in DEFAULT_HIDDEN_OPERATIONS
    for name in ("transcode_root_set", "transcode_status", "transcode_export", "transcode_apply", "schema_export", "transcode_watch_set"):
        assert get_operation_spec(name).group == "transcode"
    assert get_operation_spec("vfx_transcode_apply").group == "vfx"
    assert get_operation_spec("transcode_apply").kind == "write"


def test_ue_sync_validates_arguments(all_features) -> None:
    assert ue_sync("bogus")["error"]["code"] == "invalid_action"  # type: ignore[arg-type]
    assert ue_sync("status", paths="x")["error"]["code"] == "invalid_request"  # type: ignore[arg-type]
    assert ue_sync("push", options={"nope": 1})["error"]["code"] == "invalid_option"
    assert ue_sync("push", options={"force": "sideways"})["error"]["code"] == "invalid_option"


def test_ue_sync_runs_against_fake_bridge(all_features, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    ue = FakeUe({"mf": material_function_raw()})
    monkeypatch.setattr(runtime, "bridge", ue)
    monkeypatch.setattr(instance_manager, "repository", lambda: dict(mirror_root=str(tmp_path / "mirror"), mirror_project_name="Shadetest"))
    monkeypatch.setattr(instance_manager, "work_scope", lambda *args, **kwargs: nullcontext())
    monkeypatch.setenv("UE_NEXUS_TRANSCODE_DIR", str(tmp_path / "mirror"))
    result = ue_sync("init")
    assert result["ok"] is True, result
    assert result["data"]["counts"] == {"pulled": 1}
    status = ue_sync("status")
    assert status["ok"] is True and status["data"]["counts"] == {"clean": 1}
    lint = ue_sync("lint")
    assert lint["ok"] is True and lint["data"]["ok_files"] == 1
    assert (tmp_path / "mirror" / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus").is_file()


def test_ue_sync_reports_sync_errors_as_minimal_errors(all_features, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    def offline(operation: str, payload: dict, **_: object) -> dict:
        return {"ok": False, "operation": operation, "error": {"code": "mcp_bridge_error", "message": "no pipe"}, "diagnostics": [], "warnings": []}

    monkeypatch.setattr(tools_sync, "_bridge", offline)
    def missing_project():
        raise InstanceError("project_required", "select an exact project before resolving its repository")
    monkeypatch.setattr(instance_manager, "repository", missing_project)
    monkeypatch.setenv("UE_NEXUS_TRANSCODE_DIR", str(tmp_path / "empty"))
    result = ue_sync("status")
    assert result["ok"] is False
    assert result["error"]["code"] == "project_required"


def test_text_mirror_guide_is_served() -> None:
    body = load_guide("text_mirror")
    assert "ue_sync" in body and "@opaque" in body
    assert search_guides("push nexus text to the editor")[0]["category"] == "text_mirror"
