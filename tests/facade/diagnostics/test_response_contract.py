"""Exercise real public facade routes against the captured mixed-severity stream."""

import pytest

from tests.diagnostics.support.evidence import notice, read_response, response
from tests.support.bridges import RoutingBridge
from ue_node_nexus_mcp.server import ue_execute, ue_read
from ue_node_nexus_mcp.facade_state import facade_state

ASSET = "/Game/CharacterPure/Runtime/Control/Camera/BPC_CP_Camera.BPC_CP_Camera"


def install(use_bridge):
    return use_bridge(RoutingBridge(dict(
        diagnostics_get=response(),
        project_context_get=read_response(),
        auto_index_resolve_path=dict(ok=True, data=dict()),
        asset_get=dict(ok=True, data=dict(asset_class_path="/Script/Engine.Blueprint")),
        blueprint_details_get=read_response())))


def test_summary_prioritizes_captured_errors(all_features, use_bridge):
    install(use_bridge)
    data = ue_read(target="diagnostics")["data"]
    assert data["error_count"] == 303
    assert [item["severity"] for item in data["items"]] == ["error"] * 3
    assert sorted(item["occurrence_count"] for item in data["items"]) == [2, 4, 297]


def test_auto_outer_response_preserves_runtime_evidence(all_features, use_bridge):
    install(use_bridge)
    result = ue_read(target="auto", asset_path=ASSET)
    assert result["ok"]
    assert result["data"]["runtime_diagnostics"]["error_count"] == 303
    assert result["data"]["runtime_diagnostics"]["status"] == "failed"
    assert "remaining_errors" not in result


@pytest.mark.parametrize("mode", ("summary", "brief", "delta", "ids_only", "silent", "full"))
def test_all_operation_modes_label_scope_and_keep_notice(all_features, use_bridge, mode):
    install(use_bridge)
    result = ue_execute("project_context_get", dict(), response=dict(mode=mode))
    outer = result if mode == "full" else result["data"]
    assert outer["operation_diagnostics"]["scope"] == "operation"
    assert outer["operation_diagnostics"]["error_count"] == 0
    assert outer["runtime_diagnostics"]["error_count"] == 303
    assert outer["runtime_diagnostics"]["status"] == "failed"


def test_failed_response_keeps_runtime_notice(all_features, use_bridge):
    use_bridge(RoutingBridge(dict(project_context_get=dict(
        ok=False, error=dict(code="test_failure", message="probe failed"),
        runtime_diagnostics=notice()))))
    result = ue_execute("project_context_get", dict())
    assert not result["ok"]
    assert result["runtime_diagnostics"]["error_count"] == 303


def test_target_count_is_separate_from_session_count(all_features, use_bridge):
    raw = response()
    raw["data"]["items"] = [item for item in raw["data"]["items"] if item.get("asset_path", "").endswith("ABP_CharacterPure")]
    raw["data"]["scope"] = "/Game/Test.ABP_CharacterPure"
    raw["data"]["error_count"] = 6
    raw["data"]["runtime"]["matched_error_count"] = 6
    use_bridge(RoutingBridge(dict(diagnostics_get=raw)))
    result = ue_read(target="diagnostics", asset_path="/Game/Test.ABP_CharacterPure", format="detail", query=dict(include_history=False))
    assert result["data"]["error_count"] == 6
    assert result["runtime_diagnostics"]["error_count"] == 303
    assert "remaining_errors" not in result


def test_empty_delta_does_not_clear_cumulative_failure(all_features, use_bridge):
    raw = response()
    raw["data"]["items"] = []
    raw["data"]["returned_error_count"] = 0
    use_bridge(RoutingBridge(dict(diagnostics_get=raw)))
    data = ue_read(target="diagnostics", query=dict(cursor=328, include_history=False))["data"]
    assert data["error_count"] == 303
    assert data["runtime_diagnostics"]["status"] == "failed"
    assert data["runtime_diagnostics"]["next_read"]["args"]["query"]["session_id"] == notice()["session_id"]


def test_artifact_page_carries_original_notice(all_features, use_bridge):
    install(use_bridge)
    result = ue_read(target="auto", asset_path=ASSET)
    identifier = result["data"]["artifact"]["id"]
    raw = facade_state.get_artifact(identifier)
    assert raw is not None
    page = ue_read(target="artifact", query=dict(artifact_id=identifier, path="data", limit_bytes=128))
    assert page["data"]["runtime_diagnostics"]["error_count"] == 303
    assert page["data"]["runtime_diagnostics"]["live_state"] is False


def test_explicit_historical_read_keeps_its_selected_session(all_features, use_bridge):
    raw = response()
    raw["data"]["runtime"]["session_id"] = "historical-pie"
    raw["runtime_diagnostics"] = dict(notice(), session_id="latest-pie", error_count=0)
    use_bridge(RoutingBridge(dict(diagnostics_get=raw)))
    result = ue_read(target="diagnostics", format="detail", query=dict(session_id="historical-pie", include_history=False))
    assert result["runtime_diagnostics"]["session_id"] == "historical-pie"
    assert result["runtime_diagnostics"]["error_count"] == 303


def test_compact_postcheck_retains_its_asset_and_stage_scope(all_features, use_bridge):
    use_bridge(RoutingBridge(dict(asset_compile=dict(ok=True,
        data=dict(compile=dict(ran=True, ok=True, error_count=0)), runtime_diagnostics=notice()))))
    result = ue_execute("asset_compile", dict(asset_path=ASSET), response=dict(mode="brief"))
    assert result["remaining_errors"] == 0
    assert result["remaining_errors_scope"] == dict(asset_path=ASSET, stage="asset_postcheck")


def test_native_sample_truncation_is_preserved(all_features, use_bridge):
    value = notice()
    value["samples"] = [dict(severity="error", message="bounded native sample", sample_truncated=True)]
    use_bridge(RoutingBridge(dict(project_context_get=dict(ok=True, data=dict(), runtime_diagnostics=value))))
    result = ue_execute("project_context_get", dict())
    assert result["data"]["runtime_diagnostics"]["samples"][0]["text_truncated"]
