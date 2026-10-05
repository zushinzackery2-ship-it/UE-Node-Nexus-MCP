"""Exercise publication orchestration, durable job reports, and report budgeting."""

from concurrent.futures import ThreadPoolExecutor
from threading import Barrier
from types import SimpleNamespace

from tests.collaboration.fake_bridge import ProtocolUe
from tests.diagnostics.support.evidence import notice
from ue_node_nexus_mcp.diagnostics.contracts.observation import capture, fields, record
from ue_node_nexus_mcp.tools_sync import _finish
from ue_node_nexus_mcp.transcode.collaboration.jobs.service import report
from ue_node_nexus_mcp.transcode.push.diagnostics import apply_diagnostics
from ue_node_nexus_mcp.transcode.push.model import PushResult
from ue_node_nexus_mcp.transcode.sync.service import run_sync


def test_online_sync_retains_notice_and_offline_action_has_its_own_observation(tmp_path):
    ue = ProtocolUe()
    calls = []

    def bridge(operation, payload):
        calls.append(operation)
        return dict(ue(operation, payload), runtime_diagnostics=notice())

    environment = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "mirror"))
    published = run_sync(bridge, "checkout", options=dict(dry_run=False), env=environment)
    assert published["runtime_diagnostics"]["error_count"] == 303
    assert published["runtime_diagnostics"]["status"] == "failed"
    assert published["runtime_verification"] == "not_run"
    calls.clear()
    offline = run_sync(bridge, "workspaces", options=dict(project="Shadetest"), env=environment)
    assert calls == []
    assert offline["runtime_observed"] is False
    assert offline["runtime_diagnostics"]["status"] == "unknown"


def test_legacy_apply_keeps_runtime_separate_from_compile_failure():
    prepared = SimpleNamespace(plan=SimpleNamespace(ids=dict(), verbs=[]),
                               document=SimpleNamespace(iter_decls=lambda: iter(())))
    response = dict(ok=True, data=dict(compile=dict(ran=True, ok=True, error_count=0)),
                    diagnostics=[], runtime_diagnostics=notice())
    result = PushResult()
    errors = apply_diagnostics(response, prepared, "fixture.nexus", result)
    assert errors == []
    assert result.runtime_diagnostics["error_count"] == 303


def test_job_status_preserves_stored_runtime_evidence():
    data = report(dict(id="fixture", status="succeeded", result_id="stored",
                       runtime_diagnostics=notice(), runtime_observed=True, runtime_verification="not_run"))
    assert data["status"] == "succeeded"
    assert data["runtime_diagnostics"]["error_count"] == 303
    assert data["runtime_diagnostics"]["live_state"] is False


def test_second_report_budget_keeps_diagnostics():
    value = dict(action="push", status="published", error_count=0, runtime_diagnostics=notice(),
                 rows=[dict(asset=str(index), detail="x" * 2048) for index in range(80)])
    result = _finish(value)
    assert result["ok"]
    assert result["data"]["inline_truncated"]
    assert result["data"]["runtime_diagnostics"]["error_count"] == 303


def test_worker_observations_do_not_share_runtime_sessions():
    barrier = Barrier(2)

    def observe(session_id, errors):
        with capture():
            value = dict(notice(), session_id=session_id, error_count=errors)
            record(dict(runtime_diagnostics=value))
            barrier.wait(3)
            return fields()["runtime_diagnostics"]

    with ThreadPoolExecutor(max_workers=2) as pool:
        first = pool.submit(observe, "first", 303)
        second = pool.submit(observe, "second", 0)
        assert first.result()["session_id"] == "first"
        assert first.result()["error_count"] == 303
        assert second.result()["session_id"] == "second"
        assert second.result()["error_count"] == 0
