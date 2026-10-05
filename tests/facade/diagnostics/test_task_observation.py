"""Background operation status retains both successful and failed observations."""

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.diagnostics.contracts.observation import record
from tests.diagnostics.support.evidence import read_response


@pytest.mark.parametrize("failure", (False, True))
def test_task_poll_and_result_retain_the_executing_session(failure):
    task_queue.reset_for_tests()

    def execute():
        response = record(read_response())
        if failure:
            raise ValueError("execution failed after observation")
        return response

    identifier = task_queue.submit_callable("project_context_get", execute)
    try:
        assert task_queue.wait_for_task(identifier, 3)
        for result in (task_queue.task_status(identifier), task_queue.task_result(identifier)):
            data = result["data"]
            assert data["status"] == ("failed" if failure else "succeeded")
            assert data["runtime_diagnostics"]["error_count"] == 303
            assert data["runtime_diagnostics"]["status"] == "failed"
            assert data["runtime_diagnostics"]["live_state"] is False
    finally:
        task_queue.reset_for_tests()
