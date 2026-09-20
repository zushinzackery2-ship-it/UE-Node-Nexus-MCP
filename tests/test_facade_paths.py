"""End-to-end coverage of the live MCP facade surface (the six thin tools).

Every test here goes through the registered tool functions with a fake bridge
injected, so client-side behavior (diagnostics log enrichment, Niagara verbose
trimming, read routing, capability hiding, diff pagination) is verified on the
exact path real MCP clients use.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from tests.support.bridges import RecordingBridge, RoutingBridge

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.errors import BridgeError
from ue_node_nexus_mcp.facade_execute import preflight_execute_request
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.server import (
    ue_capability_get,
    ue_diff_get,
    ue_execute,
    ue_read,
)

_FEATURE_ENV_VARS = (
    "UE_NEXUS_FEATURES",
    "UE_NEXUS_ENABLE_FEATURES",
    "UE_NEXUS_DISABLE_FEATURES",
    "UE_NEXUS_VFX_SUPPORT",
)


def test_heavy_blueprint_graph_read_recommends_named_node_read() -> None:
    response = preflight_execute_request(
        "graph_snapshot_get",
        {
            "asset_path": "/Game/BP_Demo.BP_Demo",
            "graph_kind": "blueprint",
            "graph_name": "Replay",
            "format": "full",
            "include_node_params": True,
            "node_params_format": "full",
        },
        {},
    )

    assert response is not None
    assert response["data"]["recommended_param_read"]["payload"]["graph_name"] == "Replay"


# --- diagnostics_get enrichment ---------------------------------------------

def _material_failure_log() -> str:
    return "\n".join(
        [
            "[2026.06.12-01.00.01:000][201]LogMaterial: Warning: "
            "[AssetLog] /Game/Materials/M_Demo.M_Demo: Failed to compile Material "
            "for platform PCD3D_SM6, Default Material will be used in game.",
            "Function WorldAlignedTexture: (Node TextureSample) Sampler type is Color, "
            "should be Linear Color for /Game/Textures/T_Demo.T_Demo",
        ]
    )


def test_ue_execute_diagnostics_get_enriches_related_log_items(tmp_path: Path, all_features, use_bridge) -> None:
    logs_dir = tmp_path / "Logs"
    logs_dir.mkdir()
    (logs_dir / "Demo.log").write_text(_material_failure_log(), encoding="utf-8")
    bridge = use_bridge(
        RoutingBridge(
            {
                "diagnostics_get": {"ok": True, "data": {"items": [], "error_count": 0}},
                "project_context_get": {"ok": True, "data": {"project_saved_dir": str(tmp_path)}},
            }
        )
    )

    response = ue_execute("diagnostics_get", {}, response={"mode": "full"})

    assert response["ok"] is True
    assert [call[0] for call in bridge.calls] == ["diagnostics_get", "project_context_get"]
    data = response["data"]
    assert data["related_log_item_count"] >= 1
    codes = {item["code"] for item in data["related_log_items"]}
    assert "material_sampler_type_mismatch_from_log" in codes
    assert data["related_log_source"].endswith("Demo.log")


# --- Niagara verbose trimming ------------------------------------------------

def test_ue_execute_trims_verbose_niagara_fields(all_features, use_bridge) -> None:
    use_bridge(
        RoutingBridge(
            {
                "niagara_system_summary_get": {
                    "ok": True,
                    "data": {
                        "emitters": ["Fire"],
                        "capabilities": ["long static prose"],
                        "limitations": ["long static prose"],
                        "recommended_generic_workflow": ["long static prose"],
                    },
                }
            }
        )
    )

    response = ue_execute(
        "niagara_system_summary_get",
        {"asset_path": "/Game/FX/NS_Fire.NS_Fire"},
        response={"mode": "full"},
    )

    assert response["ok"] is True
    assert response["data"]["emitters"] == ["Fire"]
    for verbose_field in ("capabilities", "limitations", "recommended_generic_workflow"):
        assert verbose_field not in response["data"]


# --- ue_read routing ----------------------------------------------------------

def test_ue_read_routes_asset_target_to_asset_get(all_features, use_bridge) -> None:
    bridge = use_bridge(
        RoutingBridge({"asset_get": {"ok": True, "data": {"asset_path": "/Game/A.A", "asset_class_path": "/Script/Engine.Texture2D"}}})
    )

    response = ue_read(target="asset", asset_path="/Game/A.A")

    assert bridge.calls == [("asset_get", {"asset_path": "/Game/A.A"})]
    assert response["ok"] is True
    data = response["data"]
    assert data["operation"] == "asset_get"
    assert data["snapshot_token"]
    assert data["diff_token"]


def test_ue_read_debug_format_returns_raw_bridge_envelope(all_features, use_bridge) -> None:
    use_bridge(RoutingBridge({"asset_get": {"ok": True, "data": {"asset_path": "/Game/A.A"}}}))

    response = ue_read(target="asset", asset_path="/Game/A.A", format="debug")

    assert response["operation"] == "asset_get"
    assert response["data"] == {"asset_path": "/Game/A.A"}


def test_ue_read_invalid_target_is_structured_error(all_features) -> None:
    response = ue_read(target="")

    assert response["ok"] is False
    assert response["error"]["code"] == "invalid_request"


# --- capability hiding --------------------------------------------------------

def test_capability_index_hides_flagged_compat_operations(all_features) -> None:
    listed = {row[0] for row in ue_capability_get()["data"]["operations"]}
    assert "editor_save_all" not in listed
    assert "editor_request_exit" not in listed
    assert "asset_delete" in listed

    with_hidden = {row[0] for row in ue_capability_get(include_hidden=True)["data"]["operations"]}
    assert "editor_save_all" in with_hidden

    by_name = ue_capability_get(operation="editor_save_all")
    assert by_name["ok"] is True
    assert by_name["data"]["operation"] == "editor_save_all"


# --- ue_execute argument validation -------------------------------------------

def test_ue_execute_invalid_arguments_return_structured_errors(all_features) -> None:
    assert ue_execute("", {})["error"]["code"] == "invalid_request"
    assert ue_execute("asset_get", "not-an-object")["error"]["code"] == "invalid_request"  # type: ignore[arg-type]
    assert ue_execute("no_such_operation", {})["error"]["code"] == "invalid_operation"


# --- ue_diff_get pagination ----------------------------------------------------

def test_ue_diff_get_cursor_pages_through_changes(all_features, use_bridge) -> None:
    use_bridge(RoutingBridge({"node_create": {"ok": True, "data": {"node_id": "node-1"}}}))
    executed = ue_execute(
        "node_create",
        {"asset_path": "/Game/Materials/M_Test.M_Test", "node_class": "MaterialExpressionAdd", "dry_run": False},
    )
    token = executed["data"]["diff_token"]

    first = ue_diff_get(since_token=token, limit=2)
    assert first["ok"] is True
    assert first["data"]["truncated"] is True
    assert first["data"]["next_cursor"] == "2"
    assert len(first["data"]["changes"]) == 2

    second = ue_diff_get(since_token=token, limit=2, cursor=first["data"]["next_cursor"])
    assert second["ok"] is True
    assert second["data"]["truncated"] is False
    assert second["data"]["next_cursor"] is None

    combined = first["data"]["changes"] + second["data"]["changes"]
    third = ue_diff_get(since_token=token, limit=50)
    assert combined == third["data"]["changes"]


def test_ue_diff_get_invalid_arguments_return_structured_errors(all_features) -> None:
    assert ue_diff_get()["error"]["code"] == "invalid_request"
    assert ue_diff_get(since_token="diff_999999")["error"]["code"] == "token_expired"
    assert ue_diff_get(since_token="diff_1", cursor="not-a-number")["error"]["code"] == "invalid_cursor"
    assert ue_diff_get(since_token="diff_1", limit=0)["error"]["code"] == "invalid_limit"


# --- vfx feature probe caching -------------------------------------------------

@pytest.fixture
def clean_feature_state(monkeypatch: pytest.MonkeyPatch) -> None:
    for name in _FEATURE_ENV_VARS:
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setattr(runtime, "_enabled_features", None)
    monkeypatch.setattr(runtime, "_response_mode", "minimal")
    monkeypatch.setattr(runtime, "_profile_args_consumed", True)
    monkeypatch.setattr(runtime, "_cli_feature_args", (None, set(), set(), None))


class _UnreachableBridge:
    def call(self, *_args, **_kwargs):
        raise BridgeError("no UE editor instance found")


def test_inconclusive_vfx_probe_is_not_cached(clean_feature_state, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(runtime, "bridge", _UnreachableBridge())
    monkeypatch.setattr(runtime.instance_manager, "binding", dict())
    assert "vfx" not in runtime.enabled_features()
    assert runtime._enabled_features is None  # probe failure must not stick

    runtime.instance_manager.binding["vfx_available"] = True
    assert "vfx" in runtime.enabled_features()
    assert runtime._enabled_features is not None


def test_editor_without_vfx_module_is_cached(clean_feature_state, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(runtime, "bridge", _UnreachableBridge())
    monkeypatch.setattr(runtime.instance_manager, "binding", dict(vfx_available=False))
    assert "vfx" not in runtime.enabled_features()
    assert runtime._enabled_features is not None  # definitive editor answer sticks


# --- instance bind callback -----------------------------------------------------

def test_instance_bind_fires_feature_reset_callback(monkeypatch: pytest.MonkeyPatch, tmp_path) -> None:
    pipe_name = "\\\\.\\pipe\\UeNodeNexusBridge.123"
    events: list[str] = []
    project = tmp_path / "Demo.uproject"
    project.write_text("{}")
    manager = EditorSession(project=str(project))
    item = dict(instance_id="first", pid=123, state="READY", project_path=str(project), project_name="Demo")
    monkeypatch.setattr(manager, "call", lambda *_args: dict(instance=dict(item), lease=dict(lease_id="lease")))
    manager.set_on_bind_changed(lambda: events.append("bind"))
    manager.ensure(dict(dry_run=False))
    assert manager.current()["target"] == pipe_name
    assert events == ["bind"]
    manager.ensure(dict(dry_run=False))
    assert events == ["bind"]
    item["instance_id"] = "replacement"
    manager.ensure(dict(dry_run=False, instance_id="replacement"))
    assert events == ["bind", "bind"]
    assert manager.current()["mode"] == "explicit"
