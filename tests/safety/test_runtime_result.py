import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.runtime.result import wait_for_result
from ue_node_nexus_mcp.safety.isolation.inputs import validate_operations


class Session:
    timeout = 1
    rhi = "d3d12"

    def __init__(self, result):
        self.result = result

    def call(self, operation, payload):
        assert operation == "runtime_smoke_status" and payload == dict(smoke_id="smoke")
        return self.result


def test_smoke_is_an_explicit_isolated_operation(all_features):
    entries = validate_operations([dict(operation="runtime_smoke_start", payload=dict(duration_seconds=1))])
    assert entries[0]["payload"]["dry_run"] is False


def test_pie_errors_fail_even_when_the_gpu_frame_succeeded(monkeypatch):
    result = dict(ok=False, data=dict(done=True, ok=False, error_code="runtime_diagnostics_failed"))
    assert wait_for_result(Session(result), dict(data=dict(smoke_id="smoke"))) is result


def test_render_result_requires_independent_image_and_rhi_verification(monkeypatch):
    result = dict(ok=True, data=dict(done=True, ok=True, rhi="D3D12",
        session_id="pie", instance_id="worker",
        runtime=dict(session_id="pie", instance_id="worker", available=True, pie=True, active=False,
                     sources_complete=True, asset_counts_complete=True, error_count=0, dropped_count=0),
        capture=dict(capture_protocol=1, file_path="frame", capture_id="image")))
    monkeypatch.setattr("ue_node_nexus_mcp.safety.runtime.result.capture_status",
                        lambda path, identifier: dict(ok=True, data=dict(state="completed", width=1920)))
    assert wait_for_result(Session(result), dict(data=dict(smoke_id="smoke")))["data"]["verified_capture"]["width"] == 1920
    result["data"]["rhi"] = "NullRHI"
    with pytest.raises(InstanceError, match="different RHI"):
        wait_for_result(Session(result), dict(data=dict(smoke_id="smoke")))
