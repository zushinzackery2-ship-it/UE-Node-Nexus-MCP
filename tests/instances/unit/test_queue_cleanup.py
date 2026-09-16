from concurrent.futures import ThreadPoolExecutor
import threading

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.tools_facade import ue_execute
from tests.support.bridges import RecordingBridge
from tests.support.scopes import Scope


def test_shutdown_cancels_a_submission_waiting_for_its_scope(all_features, use_bridge, monkeypatch):
    task_queue.reset_for_tests()
    bridge = use_bridge(RecordingBridge())
    entered, resume = threading.Event(), threading.Event()
    scope = Scope()

    def reserve(*args):
        entered.set()
        assert resume.wait(5)
        return scope

    monkeypatch.setattr(task_queue, "reserve", reserve)
    try:
        with ThreadPoolExecutor(max_workers=1) as pool:
            submitted = pool.submit(task_queue.task_submit, "asset_get", dict(asset_path="/Game/A.A"))
            assert entered.wait(3)
            assert task_queue.task_status()["data"]["tasks"][0]["status"] == "reserving"
            task_queue.shutdown()
            resume.set()
            assert submitted.result()["data"]["status"] == "cancelled"
        assert scope.released and not bridge.calls
    finally:
        resume.set()
        task_queue.reset_for_tests()


def test_lifecycle_operations_cannot_be_hidden_inside_batches_or_tasks(all_features, use_bridge):
    use_bridge(RecordingBridge())
    items = [dict(operation="asset_get", payload=dict(asset_path="/Game/A.A")),
             dict(operation="bridge_instance_release", payload=dict())]
    result = ue_execute("batch_execute", dict(operations=items))
    assert result["error"]["code"] == "invalid_batch"
    result = ue_execute("task_submit", dict(operation="batch_execute", payload=dict(operations=items)))
    assert result["error"]["code"] == "invalid_batch"
