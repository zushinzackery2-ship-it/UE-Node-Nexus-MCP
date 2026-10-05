"""A runtime verdict must use complete observations from one explicit PIE window."""

import pytest

from tests.diagnostics.support.evidence import notice
from ue_node_nexus_mcp.diagnostics.contracts.verdict import runtime_verdict


def runtime_data(**changes):
    value = dict(notice(), available=True, pie=True, active=False, error_count=0, warning_count=0,
                 dropped_count=0, sources_complete=True, asset_counts_complete=True, unattributed_error_count=0)
    value.update(changes)
    return value


def test_error_dominates_incomplete_coverage():
    data = runtime_data(error_count=303, dropped_count=1)
    assert runtime_verdict(data)["status"] == "failed"


@pytest.mark.parametrize("changes", (
    dict(available=False), dict(active=True), dict(dropped_count=1),
    dict(cursor_gap=True), dict(asset_counts_complete=False), dict(pie=False),
    dict(sources_complete=False), dict(dropped_count=None), dict(error_count=None)))
def test_incomplete_observation_cannot_pass(changes):
    assert runtime_verdict(runtime_data(**changes))["status"] == "inconclusive"


def test_valid_completed_window_can_pass():
    assert runtime_verdict(runtime_data())["status"] == "passed"


def test_expected_session_identity_is_checked():
    result = runtime_verdict(runtime_data(), session_id="another-pie")
    assert result["status"] == "inconclusive"
    assert result["code"] == "runtime_session_mismatch"


def test_functional_pass_with_runtime_errors_is_a_failure():
    result = runtime_verdict(runtime_data(error_count=303), functional_passed=True)
    assert result["passed"] is False
    assert result["code"] == "runtime_diagnostics_failed"
