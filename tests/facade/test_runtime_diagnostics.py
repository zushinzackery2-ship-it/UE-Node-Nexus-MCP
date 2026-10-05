from tests.support.bridges import RoutingBridge
from ue_node_nexus_mcp.server import ue_execute, ue_read


def diagnostic_data():
    return dict(items=[dict(severity="error", message="MotionState is null", occurrence_count=6)],
                error_count=6, warning_count=0, assets_unsupported=1, assets_not_loaded=0,
                coverage="partial", runtime=dict(session_id="pie-one", next_cursor=6, error_count=6))


def install(use_bridge):
    return use_bridge(RoutingBridge(dict(
        diagnostics_get=dict(ok=True, data=diagnostic_data()),
        project_context_get=dict(ok=True, data=dict()))))


def test_diagnostic_detail_is_a_response_mode_without_an_extra_payload_field(all_features, use_bridge):
    bridge = install(use_bridge)
    result = ue_read(target="diagnostics", format="detail")
    assert result["ok"], result
    assert bridge.calls[0] == ("diagnostics_get", dict())
    assert result["data"]["error_count"] == 6
    assert result["data"]["items"][0]["occurrence_count"] == 6


def test_summary_exposes_diagnostic_counts_and_coverage(all_features, use_bridge):
    install(use_bridge)
    result = ue_execute("diagnostics_get", dict())
    assert result["data"]["diagnostics"]["errors"] == 6
    assert result["data"]["coverage"] == "partial"
    assert result["data"]["assets_unsupported"] == 1
    assert result["data"]["runtime"]["session_id"] == "pie-one"
    assert result["data"]["next_read"]["args"]["target"] == "diagnostics"


def test_diagnostic_read_summary_keeps_errors_visible(all_features, use_bridge):
    install(use_bridge)
    result = ue_read(target="diagnostics")
    assert result["data"]["error_count"] == 6
    assert result["data"]["coverage"] == "partial"


def test_operation_runtime_notice_survives_default_summary(all_features, use_bridge):
    use_bridge(RoutingBridge(dict(project_context_get=dict(ok=True, data=dict(project_name="Demo"),
        runtime_diagnostics=dict(session_id="pie-one", error_count=6, next_cursor=9)))))
    result = ue_execute("project_context_get", dict())
    assert result["data"]["runtime_diagnostics"]["error_count"] == 6


def test_large_diagnostic_detail_preserves_counts_and_paged_items(all_features, use_bridge, monkeypatch):
    from ue_node_nexus_mcp import facade_response

    monkeypatch.setattr(facade_response, "LARGE_RESPONSE_INLINE_BYTE_LIMIT", 512)
    data = diagnostic_data()
    data["items"] = [dict(severity="error", message="MotionState is null " + str(index), node_guid=str(index)) for index in range(60)]
    data["error_count"] = 60
    use_bridge(RoutingBridge(dict(diagnostics_get=dict(ok=True, data=data),
                                  project_context_get=dict(ok=True, data=dict()))))
    result = ue_read(target="diagnostics", format="detail")
    summary = result["data"]
    assert summary["stored_as_artifact"] and summary["error_count"] == 60 and summary["coverage"] == "partial"
    assert summary["next_read"]["args"]["target"] == "artifact"
    artifact = summary["artifact"]["id"]
    page = ue_read(target="artifact", query=dict(artifact_id=artifact, path=["data", "items"], limit_bytes=400))
    assert page["ok"] and 0 < len(page["data"]["items"]) < len(data["items"])
    assert page["data"]["items"] == data["items"][:page["data"]["next_cursor"]]
